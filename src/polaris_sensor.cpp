#include "polaris_sensor/polaris_sensor.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdio.h>
#include <boost/algorithm/string.hpp>
#include <string>
#include <bitset>

void Polaris::removeChar(std::string& str,const char c) {
    str.erase( std::remove(str.begin(), str.end(), c), str.end() );
}

int Polaris::ascii2int(char c)
{
    if(c >= 48 && c <= 57)
        return c-48;
    else if(c >= 65 && c <= 70)
        return c-65;

    return -1;
}


int Polaris::checkAnswer(const std::string& answer)
{
    if(answer.size() <= 8)
        return -1;

    if(answer.find("OKAY")!=std::string::npos)
        return 0;

    if(answer.find("ERROR")!=std::string::npos)
        return ascii2int(answer[5]*16) + ascii2int(answer[6]);

    return -1;
}

int Polaris::PortHandle2int(const std::string& handle, int base)
{
    int res = 0;
    int i = 0;
    if(handle[0] == '+' || handle[0] == '-')
        i = 1;
    for(; i < handle.size(); i++)
    {
        res = res*base + ascii2int(handle[i]);
    }
    int out = (handle[0] == '-') ? -res : res;
    return out;
}

Polaris::Polaris()
{
}
Polaris::Polaris(const std::string port,const std::vector<std::string> roms)
{
    if(!openPort(port,9600)){
        std::cerr << "Could not open port "<<port<<" at BAUD9600"<< std::endl;
        return;
    }
    setPolarisBaudRateTo(115200);

    if(!openPort(port,115200)){
        std::cerr << "Could not open port "<<port<<" at BAUD115200"<< std::endl;
        return;
    }
    std::cout << "Port open" << std::endl;

    init();
    std::cout << "Init done" << std::endl;
    clearPortHandles();
    std::cout << "Clear done" << std::endl;

    for(size_t i=0;i<roms.size();++i)
    {
        const std::string handle = requestPortHandle();
        std::cout << "Request done, handle: " << handle <<std::endl;
        loadROM(handle,roms[i]);
        std::cout << "Load done" << std::endl;

        initPortHandle(handle);
        std::cout << "Init port done" << std::endl;

        enablePortHandle(handle,'D');
        std::cout << "Enable port done" << std::endl;

        std::cout << "Port info : " << readPortInfo(handle)<< std::endl;
    }

    startTracking();
}
Polaris::~Polaris()
{
    std::cout<<"Stopping tracking"<<std::endl;
    stopTracking();
    setPolarisBaudRateTo(9600);
    if(m_port.isOpen())
        m_port.close();
}



std::string Polaris::readUntilCR(uint32_t timeout_ms)
{
    m_port.setTimeout(serial::Timeout::max(), timeout_ms, 0, timeout_ms, 0);
    std::string result =  m_port.readline(65536,"\r");
    removeChar(result,'\n');
    return result;
}

std::string Polaris::readUntilBXAnswerComplete()
{
    std::string buffer;
    int sizeLimit = 100000;
    do {
        buffer+=m_port.read(1);
        if(buffer.size() >= 4 && sizeLimit==100000)
        {
            uint16_t reply_length;
            memcpy(&reply_length,&buffer.data()[2],sizeof(uint16_t));
            sizeLimit = reply_length + 8;
        }
    } while(buffer.size() != sizeLimit);
    return buffer;
}

bool Polaris::setPolarisBaudRateTo(unsigned long baudrate)
{
    if(!m_port.isOpen()){
        std::cerr<<"Port not openned"<<std::endl;
        return true;
    }

    std::string command_comm;
    if(baudrate == 115200)
        command_comm="COMM 50001\r";
    else if(baudrate == 9600)
        command_comm="COMM 00001\r";
    else
        std::cerr<<"Baudrate "<<baudrate<<" is not supported"<<std::endl;

    size_t nwrite = m_port.write(command_comm);

    std::string answer_comm = readUntilCR();
    std::cout<<"Set baud rate answer:"<<answer_comm<<std::endl;
    if(int a = checkAnswer(answer_comm) > 0){
        std::cerr << "COMM Error : " << a << std::endl;
        return false;
    }
    return true;
}

bool Polaris::openPort(const std::string& portname, unsigned long baudrate)
{
    if(m_port.isOpen())
        m_port.close();

    m_port.setPort(portname);
    m_port.setBaudrate(baudrate);
    m_port.setBytesize(serial::eightbits);
    m_port.setParity(serial::parity_none);
    m_port.setStopbits(serial::stopbits_one);
    m_port.setFlowcontrol(serial::flowcontrol_hardware);

    try {
        m_port.open();
        m_port.write(" \r");
        if(readUntilCR().size()==0)
        {
            std::cerr<<"Resetting baudrate"<<std::endl;
            m_port.close();
            m_port.setBaudrate(115200);
            m_port.open();
            setPolarisBaudRateTo(9600);
            m_port.close();
            m_port.setBaudrate(9600);
            m_port.open();
            m_port.write("\r");
            return readUntilCR().find("ERROR")==0;
        }
    } catch (serial::IOException &e) {
        std::cerr << "Unhandled Exception: " << e.what() << std::endl;
        return false;
    }
    std::cout << "Port "<<portname<<" opened at BAUD"<<baudrate<<std::endl;
    return true;
}

void Polaris::closePort()
{
    m_port.close();
}

void Polaris::clearPortHandles()
{
    std::string command_phsr = "PHSR 01\r";
    m_port.write(command_phsr);

    std::string answer_phsr = readUntilCR();

    int nports = ascii2int(answer_phsr[0])*16 + ascii2int(answer_phsr[1]);

    std::cout<<"Clear Port handle response : "<<answer_phsr<<std::endl<<" Number of ports : "<<nports<<std::endl;
    if(nports > 0)
    {
        for(int i = 0; i < nports; i++)
        {
            int ind = 2+(nports-1)*5;

            std::string command_phf = "PHF ";
            command_phf += answer_phsr[ind];
            command_phf += answer_phsr[ind+1];
            command_phf += '\r';

            m_port.write(command_phf);

            std::string answer_phf = readUntilCR();
            if(int a = checkAnswer(answer_phf) > 0)
                std::cerr << "Error : " << a << std::endl;
        }
    }
}

void Polaris::init()
{
    static const std::string cmd("INIT \r");
    size_t nwrite = m_port.write(cmd);
    std::string answer_init = readUntilCR();
    std::cout << "Init response : "<<answer_init << std::endl;
    if(int a = checkAnswer(answer_init) > 0)
        std::cerr << "INIT Error : " << a << std::endl;
}

std::string Polaris::requestPortHandle()
{
    std::string command_phrq = "PHRQ *********1****\r";
    size_t nwrite = m_port.write(command_phrq);
    std::string handle;
    std::string answer_phrq = readUntilCR();
    std::cout << "answer_phrq : "<<answer_phrq<<std::endl;
    if(int a = checkAnswer(answer_phrq) > 0)
        std::cerr << "PHRQ Error : " << a << std::endl;
    else
    {
        handle = answer_phrq.substr(0,2);
    }
    return handle;
}

bool Polaris::loadROM(const std::string& handle, std::string filename)
{
    std::cout << "Loading "<<filename<<std::endl;
    std::ifstream file(filename.c_str(), std::ifstream::in);
    std::ostringstream data;
    unsigned char x;
    if(file.fail()) throw "Error while opening file "+filename;

    file >> std::noskipws;
    while (file >> x) {
        data <<std::uppercase<< std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(x);
    }
    file.close();


    std::string buf = data.str();

    std::string command_pvwr("");
    std::ostringstream startAddressHex;
    int nchunks = 0;
    int startAddress = 0;
    bool lastchunk = false;
    do {

        command_pvwr.clear();
        command_pvwr += "PVWR ";
        command_pvwr += handle;

        startAddressHex.str("");
        startAddressHex << std::setw(4) << std::setfill('0') << std::hex << std::uppercase <<startAddress << std::nouppercase << std::dec;

        command_pvwr += startAddressHex.str();

        for(int i = 128*nchunks; i < 128*(nchunks+1); i++)
        {
            if(i < buf.size())
                command_pvwr += buf[i];
            else
            {
                command_pvwr += '0';
                lastchunk = true;
            }
        }
        nchunks++;

        command_pvwr += '\r';
        size_t nwrite = m_port.write(command_pvwr);

        //std::cout<<"Writing "<<command_pvwr<<std::endl;
        //std::cout<<"nwrite "<<nchunks<<" size :"<<buf.size()<<std::endl;

        std::string answer_pvwr = readUntilCR();
        //std::cout<<"answer_pvwr "<<answer_pvwr<<std::endl;

        if(int a = checkAnswer(answer_pvwr) > 0)
            std::cerr << "PVWR Error : " << a << std::endl;

        startAddress += 64;

    } while(!lastchunk);
    return true;
}

void Polaris::initPortHandle(const std::string& handle)
{
    std::string command_pinit("PINIT ");
    command_pinit+=handle;
    command_pinit+='\r';
    size_t nwrite = m_port.write(command_pinit);

    std::string answer_pinit = readUntilCR();
    std::cout << "Init Port Handle answer : "<<answer_pinit<< std::endl;
    if(int a = checkAnswer(answer_pinit) > 0)
        std::cerr << "PINIT Error : " << a << std::endl;
}

void Polaris::enablePortHandle(const std::string& handle, char priority)
{

    std::string command_pena("PENA ");
    command_pena+=handle;
    command_pena+=priority;
    command_pena+='\r';

    m_port.write(command_pena);

    std::string answer_pena = readUntilCR();
    if(int a = checkAnswer(answer_pena) > 0 && !answer_pena.size())
        std::cerr << "PENA Error : " << a << std::endl;
}

void Polaris::startTracking()
{
    static const std::string command_tstart = "TSTART 80\r";
    size_t nwrite = m_port.write(command_tstart);

    std::string answer_tstart = readUntilCR();
    std::cout << (answer_tstart.find("OKAY")==0? "Starting tracking":
                                                 "Error while startin tracking")<< std::endl;
    if(int a = checkAnswer(answer_tstart) > 0)
        std::cerr << "TSTART Error : " << a << std::endl;
}

void Polaris::stopTracking()
{
    std::string command_tstop = "TSTOP \r";
    m_port.write(command_tstop);

    std::string answer_tstop = readUntilCR();
    if(int a = checkAnswer(answer_tstop) > 0)
        std::cerr << "TSTOP Error : " << a << std::endl;
}

void Polaris::readDataTX(std::string &systemStatus, std::map<int, TransformationDataTX> &map)
{
    map.clear();
    std::string command_tx = "TX 0001\r";
    m_port.write(command_tx);

    std::string answer_tx = readUntilCR();
    std::cout << "big answer : "<<answer_tx<<std::endl;
    if(int a = checkAnswer(answer_tx) > 0)
        std::cerr << "TX Error : " << a << std::endl;

    std::string nhandlesBA = answer_tx.substr(0,2);
    int nhandles = ascii2int(nhandlesBA[0])*16 + ascii2int(nhandlesBA[1]);
    std::cout << "Nhandles : "<<nhandles<<std::endl;
    int index = 2;

    for(int i = 0; i < nhandles; i++)
    {
        bool missing = false;
        TransformationDataTX td = {0};
        int handle= atoi( answer_tx.substr(index,2).c_str() );
        std::cout << "handle : "<<answer_tx.substr(index,2)<<std::endl;
        index += 2;
        std::cout  << "substr "<< answer_tx.substr(index,7)<<std::endl;
        if(answer_tx.substr(index,7) == "MISSING"){
            std::cout << "Target "<<handle<<" MISSING"<<std::endl;
            missing = true;
            index += 7;
        }
        if(!missing){
            std::string q0 = answer_tx.substr(index,6);
            td.q0 = PortHandle2int(q0,10)/10000.0;
            index += 6;

            std::string qx = answer_tx.substr(index,6);
            td.qx = PortHandle2int(qx,10)/10000.0;
            index += 6;

            std::string qy = answer_tx.substr(index,6);
            td.qy = PortHandle2int(qy,10)/10000.0;
            index += 6;

            std::string qz = answer_tx.substr(index,6);
            td.qz = PortHandle2int(qz,10)/10000.0;
            index += 6;

            std::string tx = answer_tx.substr(index,7);
            td.tx = PortHandle2int(tx,10)/100000.0;
            index += 7;

            std::string ty = answer_tx.substr(index,7);
            td.ty = PortHandle2int(ty,10)/100000.0;
            index += 7;

            std::string tz = answer_tx.substr(index,7);
            td.tz = PortHandle2int(tz,10)/100000.0;
            index += 7;

            std::string error = answer_tx.substr(index,6);
            td.error = PortHandle2int(error,10)/10000.0;
            index += 6;
        }else{
            td.q0=td.qx=td.qy=td.qz=td.tx=td.ty=td.tz=td.error=0; // 0 is false
        }
        std::string status = answer_tx.substr(index,8);
        td.status = status;
        index += 8;

        std::string number = answer_tx.substr(index,8);
        td.number = number;
        index += 8;

        map[handle] = td;
    }
    systemStatus.clear();
    systemStatus = answer_tx.substr(index,4);
    return;// map;
}

void Polaris::readDataBX(uint16_t& systemStatus, std::map<int, TransformationDataBX>& map)
{
    map.clear();
    static const std::string command_bx("BX 0001\r");
    m_port.write(command_bx);

    std::string answer_bx= readUntilBXAnswerComplete();
    if(int a = checkAnswer(answer_bx) > 0)
        std::cerr << "BX Error : " << a << std::endl;

    uint16_t start_sequence = 0;
    memcpy(&start_sequence,answer_bx.data(),2);

    if(start_sequence != 0xA5C4)
    {
        std::cerr << "Invalid answer to BX " << std::hex << std::uppercase<< start_sequence << std::endl;
        return;
    }

    uint16_t reply_length = 0;
    memcpy(&reply_length,&answer_bx.data()[2],2);

    uint16_t header_crc = 0;
    memcpy(&header_crc,&answer_bx.data()[4],2);

    uint8_t nhandles = answer_bx.data()[6];

    int index = 7;
    for(int i = 0; i < nhandles; i++)
    {
        uint8_t nhandle = answer_bx.data()[index];
        index++;

        TransformationDataBX td = {0};

        td.handleStatus = answer_bx.data()[index];
        index++;
        float val=0.0; // TODO: Check on 32b machines
        if(td.handleStatus != 4) // missing
        {
            if(td.handleStatus == 1)
            {
                memcpy(&val,&answer_bx.data()[index],4);
                td.q0 = val;
                index += 4;

                memcpy(&val,&answer_bx.data()[index],4);
                td.qx = val;
                index += 4;

                memcpy(&val,&answer_bx.data()[index],4);
                td.qy = val;
                index += 4;

                memcpy(&val,&answer_bx.data()[index],4);
                td.qz = val;
                index += 4;

                memcpy(&val,&answer_bx.data()[index],4);
                td.tx = val/1000.0;
                index += 4;

                memcpy(&val,&answer_bx.data()[index],4);
                td.ty = val/1000.0;
                index += 4;

                memcpy(&val,&answer_bx.data()[index],4);
                td.tz = val/1000.0;
                index += 4;

                memcpy(&val,&answer_bx.data()[index],4);
                td.error = val/1000.0;
                index += 4;
            }

            memcpy(&td.portStatus,&answer_bx.data()[index],4);
            index += 4;

            memcpy(&td.number,&answer_bx.data()[index],4);
            index += 4;

            map[nhandle] = td;
        }
    }
    memcpy(&systemStatus,&answer_bx.data()[index],2);
    return;
}

std::string Polaris::readPortInfo(const std::string& handle)
{
    std::string command_phinf("PHINF ");
    command_phinf+=handle;
    command_phinf+="0001";
    command_phinf+='\r';
    m_port.write(command_phinf);

    std::string answer_phinf = readUntilCR();
    if(int a = checkAnswer(answer_phinf) > 0)
        std::cerr << "PHINF Error : " << a << std::endl;
    return answer_phinf;
}

std::string Polaris::readToolSerialNumber(const std::string& handle)
{
    std::string command_phinf("PHINF ");
    command_phinf+=handle;
    command_phinf+="0001";
    command_phinf+='\r';
    m_port.write(command_phinf);

    std::string answer_phinf = readUntilCR();
    if(int a = checkAnswer(answer_phinf) > 0)
        std::cerr << "PHINF Error : " << a << std::endl;

    // extract serial number from answer
    answer_phinf.erase(0,23);
    answer_phinf.erase(8,8);

    return answer_phinf;
}

std::string Polaris::readToolPortNumber(const std::string& handle)
{
    std::string command_phinf("PHINF ",6);
    command_phinf+=handle;
    command_phinf+="0020";
    command_phinf+='\r';
    m_port.write(command_phinf);

    std::string answer_phinf = readUntilCR();
    if(int a = checkAnswer(answer_phinf) > 0)
        std::cerr << "PHINF Error : " << a << std::endl;

    // extract port number from answer
    answer_phinf.erase(0,10);
    answer_phinf.erase(2,answer_phinf.size()-2);

    return answer_phinf;
}
