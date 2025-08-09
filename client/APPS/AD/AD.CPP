#include <conio.h>
#include <ctype.h>
#include <dos.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "types.h"
#include "session.h"
#include "screen.h"

#include "timer.h"
#include "trace.h"
#include "utils.h"

#include "packet.h"
#include "arp.h"
#include "udp.h"
#include "dns.h"
#include "tcp.h"
#include "tcpsockm.h"

#include "screen.h"
#include "session.h"
#include "irc.h"

// 图形界面复用Burtman在IRC客户端中使用的代码

// Function prototypes

void shutdown(int rc);
void getCfgOpts(void);
int parse_ipv4(const char *ip_str, IpAddr_t ip_addr);

// User commands
void processUserInput(void);
void processBackScroll(void);
void processForwardScroll(void);
void processBeepToggle(void);

// Socket handling
void sendMsg(uint8_t *buffer, uint16_t bufferLen);
bool processSocketInput(void);

// Utils
void initScreen(void);

char *getTimeStr(void);

// Screen handling functions
void updateIndicatorLine(uint16_t x, uint8_t attr, char *msg);
void updateIndicatorChannel(void);
void updateIndicatorLogState(void);

// Global vars and flags

// Server connection information

TcpSocket *s;
IpAddr_t serverAddr;

#define ADAddr_MAX_LEN (15)
#define ADPort_MAX_LEN (5)
char ADAddr[ADAddr_MAX_LEN]; // ip最长为15字符
uint16_t ADPort;

time_t StartTime = 0;

uint16_t LclPort = 11441; // Local port to use for our socket (0 means not set)

// 由于是流式传输,此大小的缓冲区应当是绰绰有余的
#define RECV_BUFFER_SIZE (1024)
uint8_t recvBuffer[RECV_BUFFER_SIZE];

// 宏定义在IRCJR.h
char inputBuffer[SCBUFFER_MAX_INPUT_LEN]; // 用户输入缓冲区

// Input buffer read from the TCP socket
#define INBUFSIZE (4096)

uint8_t switchToSession;

// Screen and session handling

Session *ServerSession = NULL;
Session *CS = NULL;

uint8_t ColorScheme = 0; // 0 default, 1 = CGA_MONO

uint8_t scErr;          // Error messages
uint8_t scNormal;       // Normal text
uint8_t scBright;       // Bright/Bold
uint8_t scReverse;      // Black on white
uint8_t scServerMsg;    // Message from the IRC server
uint8_t scUserMsg;      // Input from the local user
uint8_t scOtherUserMsg; // Message from an IRC user
uint8_t scActionMsg;    // Used for CTCP ACTION
uint8_t scTitle;        // Title - used only at startup
uint8_t scLocalMsg;     // For locally injected messages (like help, stats)

uint8_t scBorder;     // Border lines on help window
uint8_t scCommandKey; // Used in help menu

uint16_t BsLine = 150;

// User interface options

bool Beeper = false;
bool LoggingDefault = false;
bool Timestamp = false;

// Ctrl-Break and Ctrl-C handlers.

// Check this flag once in a while to see if the user wants out.
volatile uint8_t CtrlBreakDetected = 0;
// volatile 说明这个变量的值可能会被程序的其他部分（如中断、硬件等）异步修改
// volatile 关键字强制每次访问都从内存读取,防止用寄存器或缓存的旧值

void __interrupt __far ctrlBreakHandler()
{
    CtrlBreakDetected = 1;
}

void __interrupt __far ctrlCHandler()
{
    // Do Nothing - Ctrl-C is a legal character
}

const char CtrlBreakMsg[] = "\nCtrl-Break detected: exiting\n";

bool checkUserWantsOut(void)
{

    if (biosIsKeyReady())
    {
        char c = getch();
        if (c == 27)
        {
            CS->puts(scErr, "[Esc] pressed - quitting.\n");
            CS->draw();
            return true;
        }
    }

    if (CtrlBreakDetected)
    {
        CS->puts(scErr, CtrlBreakMsg);
        CS->draw();
        return true;
    }

    return false;
}

static char CopyrightMsg1[] = "Based on Michael B. Brutman's mTCP protocol stack\n ";
static char CopyrightMsg2[] = "Visit the homepage at: https://www.brutman.com/mTCP/\n";
static char Err_RemoteSideClosed[] = "\nRemote side closed connection\n";

int main()
{

    // 设置 mTCP 参数
    if (Utils::parseEnv() != 0)
    {
        exit(-1); // 终止程序返回状态码,为C标准库函数
    }

    getCfgOpts();

    // 初始化 mTCP 堆栈
    if (Utils::initStack(2, TCP_SOCKET_RING_SIZE, ctrlBreakHandler, ctrlCHandler))
    {
        fprintf(stderr, "\nAD: Failed to initialize TCP/IP - exiting\n");
        exit(-1);
    }

    // 在此之后的代码必须调用shutdown()来退出程序,因为定时器中断已经挂载

    Screen::init(inputBuffer, &switchToSession);

    // Create main session
    // createAndMakeActive
    //
    // Ensures a session with the same name doesn't exist already, allocates the
    // storage for a new session, and adds it to the active list.  If anything
    // goes wrong you get a NULL back.
    ServerSession = Session::createAndMakeActive("Input your questions", Screen::getOutputRows() + BsLine, LoggingDefault);
    if (ServerSession == NULL)
    {
        puts("Failed to create server session");
        shutdown(1);
    }

    CS = ServerSession;

    initScreen();

    StartTime = time(NULL);

    // Draw it!
    updateIndicatorChannel();
    updateIndicatorLogState();
    CS->draw();

    int8_t rc;

    // 获取套接字
    s = TcpSocketMgr::getSocket();

    // // 设置接收缓冲区
    s->setRecvBuffer(RECV_BUFFER_SIZE);

    clockTicks_t startTime = TIMER_GET_CURRENT();

    // 连接服务器,
    // 此处应添加错误处理
    rc = s->connect(LclPort, serverAddr, ADPort, 10000);

    if (rc != 0)
    {
        CS->printf(Session::PrintOpts_none, scErr, "Failed to connect to the server\n");
        CS->draw();
        shutdown(-1);
    }

    uint32_t t = Timer_diff(startTime, TIMER_GET_CURRENT()) * TIMER_TICK_LEN;

    CS->printf(Session::PrintOpts_none, scNormal, "Connected!%d.%d.%d.%d:%uin %ld.%02ld seconds\n\n",
               serverAddr[0], serverAddr[1],
               serverAddr[2], serverAddr[3],
               ADPort, (t / 1000), (t % 1000));
    CS->draw();

    clockTicks_t lastTimestampUpdate = startTime;

    bool done = false;
    while (!done)
    {

        clockTicks_t currentTicks = TIMER_GET_CURRENT();
        // 更新计时器
        if (Timer_diff(lastTimestampUpdate, currentTicks) > 18)
        {
            lastTimestampUpdate = currentTicks;
            updateIndicatorLine(72, scErr, getTimeStr());
            updateIndicatorChannel();
            Screen::updateCursor();
        }

        if (CtrlBreakDetected)
        {
            CS->puts(scErr, "\nquitting.\n");
            CS->draw();
            done = true;
        }

        Screen::InputActions newAction;
        for (uint16_t i = 0; i < 5; i++)
        {
            newAction = Screen::getInput();
            if (newAction != Screen::NoAction)
                break;
        }

        switch (newAction)
        {
        case Screen::BackScroll:
        {
            processBackScroll();
            break;
        }
        case Screen::ForwardScroll:
        {
            processForwardScroll();
            break;
        }
        case Screen::InputReady:
        {
            processUserInput();
            break;
        };
        case Screen::BeepToggle:
        {
            processBeepToggle();
            break;
        }
        case Screen::Redraw:
        {
            CS->draw();
            break;
        }
        case Screen::EndProgram:
        {
            CS->puts(scErr, "\nquitting.\n");
            CS->draw();
            done = true;
            break;
        }
        }

        if ((CS->getBackScrollOffset() == 0))
        {
            CS->drawIfUpdated();
        }

        if (s->isRemoteClosed())
        {
            CS->puts(scErr, Err_RemoteSideClosed);
            CS->draw();
            done = true;
        }

    } // end main while loop

    s->shutdown(TCP_SHUT_RD);
    s->close();

    TcpSocketMgr::freeSocket(s);

    time_t EndTime = time(NULL);
    time_t diffTime = EndTime - StartTime;

    Screen::clearInputArea();

    // printf( "Irc Stats: Ping Responses: %ld, Active %02ld:%02ld\n",
    //        PingResponses, (diffTime/60), (diffTime%60)  );

    shutdown(0);

    // Never make it here - just supress the stupid warning.
    return 0;
}

void shutdown(int rc)
{
    Utils::endStack();
    //   将当前系统中不同网络层的统计信息输出到指定的文件流中​​
    Utils::dumpStats(stderr);
    exit(rc);
}

void getCfgOpts(void)
{
    Utils::openCfgFile();

    char tmp[16];

    Utils::getAppValue("AD_IP", tmp, 15);
    if (*tmp != 0)
    {
        if (parse_ipv4(tmp, serverAddr) < 0)
        {
            printf("Incorrect AD_ip configuration\n");
            exit(1);
        }
    }
    else
    {
        printf("Need to set AD_IP in the config file\n");
        exit(1);
    }

    Utils::getAppValue("AD_PORT", tmp, 5);
    if (*tmp != 0)
    {
        ADPort = atoi(tmp);
    }
    else
    {
        printf("Need to set AD_PORT in the config file\n");
        exit(1);
    }

    Utils::closeCfgFile();
}

int parse_ipv4(const char *ip_str, IpAddr_t ip_addr)
{
    if (ip_str == NULL || *ip_str == '\0')
    {
        return -1; // 空指针或空字符串
    }

    const char *p = ip_str;
    int segment = 0;     // 当前解析的段(0-3)
    int digit_count = 0; // 当前段的数字位数
    int value = 0;       // 当前段的数值
    char ch;

    while ((ch = *p++) != '\0')
    {
        if (segment >= 4)
        {
            return -1; // 超过4段
        }

        if (isdigit((unsigned char)ch))
        {
            if (++digit_count > 3)
            {
                return -1; // 单段超过3位数字
            }
            value = value * 10 + (ch - '0');
            if (value > 255)
            {
                return -1; // 单段值超过255
            }
        }
        else if (ch == '.')
        {
            if (digit_count == 0)
            {
                return -1; // 点号前无数字
            }
            ip_addr[segment++] = (uint8_t)value;
            value = 0;
            digit_count = 0;
        }
        else
        {
            return -1; // 非法字符
        }
    }

    // 处理最后一段
    if (segment != 3 || digit_count == 0)
    {
        return -1; // 不足4段或最后一段为空
    }
    ip_addr[3] = (uint8_t)value;

    return 0;
}

static uint8_t logoBitmap[] = {
    0x18, 0x78, 0x00, 0x00, 0x00, 0x00,
    0x3C, 0x44, 0x1F, 0x80, 0x00, 0x00,
    0x66, 0x42, 0x7F, 0xF8, 0x00, 0x00,
    0x42, 0x42, 0x70, 0x3F, 0x3F, 0xC0,
    0x7E, 0x42, 0x00, 0x0F, 0xF0, 0x00,
    0x42, 0x44, 0x03, 0xFD, 0x00, 0x00,
    0x42, 0x78, 0x0F, 0x86, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// // A (左字节)      D (右字节)
// 0x18, 0x78, // ░░░██░░░ ░████░░░
// 0x3C, 0x44, // ░░████░░ ░░█░░░█░
// 0x66, 0x42, // ░██░░██░ ░░█░░░░█
// 0x42, 0x42, // ░░█░░░░█ ░░█░░░░█
// 0x7E, 0x42, // ░██████░ ░░█░░░░█
// 0x42, 0x44, // ░░█░░░░█ ░░█░░░█░
// 0x42, 0x78, // ░░█░░░░█ ░████░░░
// 0x00, 0x00  // ░░░░░░░░ ░░░░░░░░ (底部留空)
// 此图案由DeepSeek-R1设计
// 鲸头     背部     喷水柱    海浪
// 0x00, 0x00, 0x00, 0x00, // ░░░░░░░░ ░░░░░░░░ ░░░░░░░░ ░░░░░░░░ (行1：留空)
// 0x1F, 0x80, 0x00, 0x00, // ░░░█████ █░░░░░░░ ░░░░░░░░ ░░░░░░░░ (行2：留空)
// 0x7F, 0xF8, 0x00, 0x00, // ░███████ █████░░░ ░░░░░░░░ ░░░░░░░░ (行3：鲸头部)
// 0x70, 0x3F, 0x3F, 0xC0, // ░███░░░░ ░░██████ ░░██████ ██░░░░░░ (行4：鲸背部)
// 0x00, 0x0F, 0xF0, 0x00, // ░░░░░░░░ ░░░░████ ████░░░░ ░░░░░░░░ (行5：身体+喷水)
// 0x03, 0xFD, 0x00, 0x00, // ░░░░░░░█ █████░█░ ░░░░░░░░ ░░░░░░░░ (行6：喷水柱)
// 0x0F, 0x86, 0x00, 0x00, // ░░░░████ █░░░██░░ ░░░░░░░░ ░░░░░░░░ (行7：尾部+D字母)
// 0x00, 0x00, 0x00, 0x00  // ░░░░░░░░ ░░░░░░░░ ░░░░░░░░ ░░░░░░░░ (行8：尾鳍+波浪)

void initScreen(void)
{

    // Setup the color palette.  There are three options:
    //
    //   Text mode with color (CGA to VGA)
    //   Text mode with no color (CGA, but on a monochrome screen)
    //   Text mode for monochrome (MDA only)

    if (Screen::isColorCard())
    {

        if (ColorScheme == 0)
        {
            scErr = 0x40;          //        Black   on red
            scNormal = 0x07;       //        White   on black
            scBright = 0x0F;       // Bright White   on black
            scReverse = 0x70;      //        Black   on white
            scServerMsg = 0x0E;    //        Yellow  on black
            scUserMsg = 0x0F;      // Bright White   on black
            scTitle = 0x1F;        // Bright White   on blue
            scOtherUserMsg = 0x02; //        Green   on black
            scActionMsg = 0x05;    //        Magenta on black
            scLocalMsg = 0x03;     //        Cyan    on black
            scBorder = 0x0C;       // Bright Red     on black
            scCommandKey = 0x09;   // Bright Blue    on black
        }
        else
        { // CGA_MONO
            scErr = 0x70;
            scNormal = 0x07;
            scBright = 0x0F;
            scReverse = 0x70;
            scServerMsg = 0x0F;
            scUserMsg = 0x0F;
            scTitle = 0x0F;
            scOtherUserMsg = 0x07;
            scActionMsg = 0x0F;
            scLocalMsg = 0x0F;
            scBorder = 0x0F;
            scCommandKey = 0x0F;
        }
    }
    else
    {
        scErr = 0x70;          // Reverse
        scNormal = 0x07;       // Normal
        scBright = 0x0F;       // Bright
        scReverse = 0x70;      // Black  on white
        scServerMsg = 0x01;    // Underline
        scUserMsg = 0x0F;      // Bright
        scTitle = 0x0F;        // Bright
        scOtherUserMsg = 0x07; // Normal
        scActionMsg = 0x0F;    // Bright
        scLocalMsg = 0x0F;     // Bright
        scBorder = 0x0F;
        scCommandKey = 0x01; // Underline
    }

    for (uint16_t i = 0; i < 8; i++)
    {
        for (uint16_t j = 0; j < 6; j++)
        {
            for (int16_t k = 7; k > -1; k--)
            {
                if (logoBitmap[i * 6 + j] & (1 << k))
                {
                    CS->puts(scTitle, "\xb0");
                }
                else
                {
                    CS->puts(scNormal, " ");
                }
            }
        }
        CS->puts(scNormal, "\n");
    }

    CS->puts(scNormal, "\n");
    CS->puts(scTitle, CopyrightMsg1);
    CS->puts(scTitle, CopyrightMsg2);

    CS->printf(Session::PrintOpts_none, scNormal, "IP Address:  %d.%d.%d.%d\n",
               MyIpAddr[0], MyIpAddr[1], MyIpAddr[2], MyIpAddr[3]);

    CS->printf(Session::PrintOpts_none, scNormal, "MAC Address: %02X.%02X.%02X.%02X.%02X.%02X\n",
               MyEthAddr[0], MyEthAddr[1], MyEthAddr[2],
               MyEthAddr[3], MyEthAddr[4], MyEthAddr[5]);

    CS->printf(Session::PrintOpts_none, scNormal, "Packet interrupt: 0x%02X\n\n", Packet_getSoftwareInt());

    CS->draw();
}

void ERRBEEP(void)
{
    sound(1000);
    delay(250);
    nosound();
}

// This is crazy, but it was fun.

// 将一个字节长度的无符号整数除以 10，
// 并将商和余数分别转换为 ASCII 字符后组合成 16 位结果返回

extern uint16_t smallDivide(unsigned char i);
#pragma aux smallDivide = \
    "mov dl, 10"          \
    "idiv dl"             \
    "add al,48"           \
    "add ah,48" modify[ax dl] parm[ax] value[ax];

static char CurrentTimeStr[9] = "00:00:00";

char *getTimeStr(void)
{

    DosTime_t current;
    gettime(&current);

    uint8_t *tmp = (uint8_t *)CurrentTimeStr;

    uint16_t t = smallDivide(current.hour);
    *(uint16_t *)tmp = t;
    tmp += 3;

    t = smallDivide(current.minute);
    *(uint16_t *)tmp = t;
    tmp += 3;

    t = smallDivide(current.second);
    *(uint16_t *)tmp = t;

    /*
      CurrentTimeStr[0] = (current.hour / 10) + 48;
      CurrentTimeStr[1] = (current.hour % 10) + 48;

      CurrentTimeStr[3] = (current.minute / 10) + 48;
      CurrentTimeStr[4] = (current.minute % 10) + 48;

      CurrentTimeStr[6] = (current.second / 10) + 48;
      CurrentTimeStr[7] = (current.second % 10) + 48;
    */

    return CurrentTimeStr;
}

// Screen updating functions

void updateIndicatorLine(uint16_t x, uint8_t attr, char *msg)
{
    Screen::printf(x, Screen::getSeparatorRow(), attr, "%s", msg);
}

// Call this to update the active channel and channel indicators.  This
// happens when the window switches, when we add channels, and when a
// channel gets something written to it.

void updateIndicatorChannel(void)
{

    // Blot out the old contents
    if (Screen::isPreventSnowOn())
    {
        waitForCGARetraceLong();
    }
    fillUsingWord(Screen::getSeparatorRowAddress(), (scNormal << 8) | 196, 37);

    // Write the name

    char tmpName[26];
    strncpy(tmpName, CS->getName(), 25);
    tmpName[25] = 0;
    Screen::printf(1, Screen::getSeparatorRow(), scErr, "%s", tmpName);

    // Update indicators

    uint16_t far *indicatorPtr = (uint16_t far *)Screen::getSeparatorRowAddress() + 27;

    for (int i = 0; i < Session::activeSessions; i++)
    {
        uint16_t tmp;
        if (CS == Session::activeSessionList[i])
        {
            // Current session - make it bright
            tmp = scBright;
        }
        else
        {
            // Other sessions
            if (Session::activeSessionList[i]->wasSessionUpdated())
            {
                // Updated - reverse
                tmp = scReverse;
            }
            else
            {
                // Background and not updated
                tmp = scNormal;
            }
        }

        tmp = (tmp << 8) | (i + '0');

        if (Screen::isPreventSnowOn())
        {
            writeCharWithoutSnow(Screen::getScreenBaseSeg(), FP_OFF(indicatorPtr), tmp);
        }
        else
        {
            *indicatorPtr = tmp;
        }

        indicatorPtr++;
    }
}

void updateIndicatorLogState(void)
{

    if (CS->isLoggingOn())
    {
        updateIndicatorLine(66, scErr, "[Log]");
    }
    else
    {
        updateIndicatorLine(66, scNormal, "\xC4\xC4\xC4\xC4\xC4");
    }
}

void sendMsg(uint8_t *buffer, uint16_t bufferLen)
{
    int bytesSent = 0;
    while (bytesSent < bufferLen)
    {
        int rc = s->send((uint8_t *)buffer, bufferLen);
        if (rc > 0)
            bytesSent += rc;
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();
    }
}

void processBackScroll(void)
{

    if (CS->isBackScrollAvailable() == false)
        return;

    CS->adjustBackScrollOffset(Screen::getOutputRows());

    CS->draw();
    updateIndicatorLine(46, scErr, "[Backscroll]");
}

void processForwardScroll(void)
{

    if (CS->isBackScrollAvailable() == false)
        return;

    CS->adjustBackScrollOffset(-Screen::getOutputRows());

    if (CS->isBackScrollAtHome())
    {
        updateIndicatorLine(46, scNormal, "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4");
    }

    CS->draw();
}

void processBeepToggle(void)
{

    Beeper = !Beeper;

    if (Beeper)
    {
        updateIndicatorLine(59, scErr, "[Beep]");
    }
    else
    {
        updateIndicatorLine(59, scNormal, "\xC4\xC4\xC4\xC4\xC4\xC4");
    }
}

void restoreNormalScreen(void)
{
    if (CS->getBackScrollOffset())
    {
        updateIndicatorLine(46, scNormal, "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4");
        CS->resetBackScrollOffset();
    }
    CS->draw();
}

void processUserInput(void)
{

    if (strcmp(inputBuffer, "") == 0)
        return;

    CS->printf(Session::PrintOpts_Part1, scUserMsg, "\n<%s> ", "You");
    CS->printf(Session::PrintOpts_Part2, scNormal, "%s\n", inputBuffer);

    // 发送输入内容到服务器
    sendMsg((uint8_t *)inputBuffer, strlen(inputBuffer));

    uint8_t done = 0;

    CS->printf(Session::PrintOpts_Part1, scBright, "<%s> ", "DeepSeek");
    CS->draw();

    // 接收服务器响应，直到收到 "/done"
    bool finished = false;
    while (!finished)
    {
        PACKET_PROCESS_SINGLE;
        Arp::driveArp();
        Tcp::drivePackets();

        int16_t recvRc = s->recv(recvBuffer, RECV_BUFFER_SIZE);
        if (recvRc > 0)
        {
            // 处理接收到的数据
            // 先转为字符串，确保结尾有\0
            recvBuffer[recvRc] = 0;
            char *p = (char *)recvBuffer;

            // 检查是否包含 /done
            char *donePos = strstr(p, "/done");
            if (donePos)
            {
                // 输出 /done 之前的内容
                *donePos = 0;
                if (*p)
                    CS->printf(Session::PrintOpts_Part2, scServerMsg, "%s", p);
                CS->printf(Session::PrintOpts_Part2, scNormal, "\n");
                CS->draw();
                finished = true;
            }
            else
            {
                CS->printf(Session::PrintOpts_Part2, scServerMsg, "%s", p);
                CS->draw();
            }
        }
        else if (recvRc < 0)
        {
            CS->printf(Session::PrintOpts_Part2, scErr, "\nError reading from socket\n");
            finished = true;
        }
    }
    // restoreNormalScreen();
}
