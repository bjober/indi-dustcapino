#include "dustcapino.h"
#include <termios.h>
#include <cstring>
#include <unistd.h>

static std::unique_ptr<DustCapIno> dustcapino(new DustCapIno());

DustCapIno::DustCapIno() :
    INDI::LightBoxInterface(this),
    INDI::DustCapInterface(this)
{
    setVersion(1, 2);
    setDriverInterface(AUX_INTERFACE | LIGHTBOX_INTERFACE | DUSTCAP_INTERFACE);
}

const char *DustCapIno::getDefaultName()
{
    return "DustCapIno";
}

bool DustCapIno::initProperties()
{
    INDI::DefaultDevice::initProperties();

    addAuxControls();

    INDI::LightBoxInterface::initProperties("Main Control",
                                            INDI::LightBoxInterface::CAN_DIM);

    INDI::DustCapInterface::initProperties("Main Control");

    SerialPortTP[0].fill("PORT", "Port", "AUTO");
    SerialPortTP.fill(getDeviceName(), "DEVICE_PORT", "Serial",
                      OPTIONS_TAB, IP_RW, 60, IPS_IDLE);
    defineProperty(SerialPortTP);

    BaudRateSP[0].fill("9600", "9600", ISS_OFF);
    BaudRateSP[1].fill("19200", "19200", ISS_OFF);
    BaudRateSP[2].fill("57600", "57600", ISS_OFF);
    BaudRateSP[3].fill("115200", "115200", ISS_ON);

    BaudRateSP.fill(getDeviceName(), "BAUD_RATE", "Serial",
                    OPTIONS_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
    defineProperty(BaudRateSP);

    SafetyOverrideSP[0].fill("SAFE", "Safety ON", ISS_ON);
    SafetyOverrideSP[1].fill("OVERRIDE", "Override", ISS_OFF);

    SafetyOverrideSP.fill(getDeviceName(), "LIGHT_SAFETY",
                          "Light Safety", MAIN_CONTROL_TAB,
                          IP_RW, ISR_1OFMANY, 0, IPS_OK);
    defineProperty(SafetyOverrideSP);

    CoverAngleNP[0].fill("ANGLE", "Angle", "%.f", 0, 270, 1, 135);
    CoverAngleNP.fill(getDeviceName(), "COVER_ANGLE",
                      "Cover Position", MAIN_CONTROL_TAB,
                      IP_RW, 0, IPS_IDLE);
    defineProperty(CoverAngleNP);

    EnvNP[0].fill("TEMPERATURE", "Temperature (C)", "%.2f", -40, 80, 0, 0);
    EnvNP[1].fill("HUMIDITY", "Humidity (%)", "%.2f", 0, 100, 0, 0);

    EnvNP.fill(getDeviceName(), "DHT_ENV",
               "Environment", MAIN_CONTROL_TAB,
               IP_RO, 0, IPS_IDLE);
    defineProperty(EnvNP);

    return true;
}

bool DustCapIno::updateProperties()
{
    if (!INDI::DefaultDevice::updateProperties())
        return false;

    INDI::LightBoxInterface::updateProperties();
    INDI::DustCapInterface::updateProperties();

    return true;
}

bool DustCapIno::Connect()
{
    int baud = 9600;
    if (BaudRateSP[1].getState() == ISS_ON) baud = 19200;
    if (BaudRateSP[2].getState() == ISS_ON) baud = 57600;
    if (BaudRateSP[3].getState() == ISS_ON) baud = 115200;

    const char *port = SerialPortTP[0].getText();

    if (strcmp(port, "AUTO") == 0)
    {
        if (!autoDetectPort())
            return false;
    }
    else
    {
        if (tty_connect(port, baud, 8, 0, 1, &PortFD) != TTY_OK)
        {
            LOGF_ERROR("Failed to open %s", port);
            return false;
        }
    }

    LOGF_INFO("Connected @ %d baud", baud);

    usleep(2000000);
    tcflush(PortFD, TCIOFLUSH);

    int written = 0;
    tty_write_string(PortFD, "CMD:STATUS\n", &written);

    SetTimer(1000);
    return true;
}

bool DustCapIno::Disconnect()
{
    if (PortFD > 0)
        tty_disconnect(PortFD);

    PortFD = -1;
    return true;
}

bool DustCapIno::autoDetectPort()
{
    const char* prefixes[] = {"/dev/ttyUSB", "/dev/ttyACM"};

    for (const char* prefix : prefixes)
    {
        for (int i = 0; i < 6; i++)
        {
            char port[32];
            snprintf(port, sizeof(port), "%s%d", prefix, i);

            int fd = -1;

            if (tty_connect(port, 115200, 8, 0, 1, &fd) != TTY_OK)
                continue;

            usleep(2000000);
            tcflush(fd, TCIOFLUSH);

            int written = 0;
            tty_write_string(fd, "CMD:STATUS\n", &written);

            char buffer[128] = {0};
            int nbytes_read = 0;

            int rc = tty_read_section(fd, buffer, '\n', 3, &nbytes_read);

            if (rc == TTY_OK && nbytes_read > 0 &&
                strncmp(buffer, "STATUS:", 7) == 0)
            {
                LOGF_INFO("Auto-detected DustCapIno on %s", port);

                PortFD = fd;

                SerialPortTP[0].setText(port);
                SerialPortTP.setState(IPS_OK);
                SerialPortTP.apply();

                return true;
            }

            tty_disconnect(fd);
        }
    }

    LOG_ERROR("Auto-detect failed");
    SerialPortTP.setState(IPS_ALERT);
    SerialPortTP.apply();
    return false;
}

void DustCapIno::TimerHit()
{
    if (!isConnected() || PortFD <= 0)
    {
        SetTimer(1000);
        return;
    }

    char buffer[128] = {0};
    int nbytes_read = 0;

    int rc = tty_read_section(PortFD, buffer, '\n', 5, &nbytes_read);

    if (rc != TTY_OK)
    {
        LOG_ERROR("Serial read error – ALERT");

        capIsMoving = false;
        movingCounter = 0;
        statusTimeoutCounter = 0;

        ParkCapSP.setState(IPS_ALERT);
        ParkCapSP.apply();

        LightSP.setState(IPS_ALERT);
        LightSP.apply();

        SetTimer(2000);
        return;
    }

    statusTimeoutCounter++;
    if (statusTimeoutCounter > 10)
    {
        LOG_WARN("No STATUS received");
        ParkCapSP.setState(IPS_ALERT);
        ParkCapSP.apply();
        statusTimeoutCounter = 0;
    }

    if (capIsMoving)
    {
        movingCounter++;
        if (movingCounter > 20)
        {
            LOG_ERROR("MOVING timeout");
            capIsMoving = false;
            movingCounter = 0;
            ParkCapSP.setState(IPS_ALERT);
            ParkCapSP.apply();
        }
    }

    if (nbytes_read == 0)
    {
        SetTimer(capIsMoving ? 250 : 1000);
        return;
    }

    buffer[strcspn(buffer, "\r\n")] = 0;
    LOGF_INFO("RX: %s", buffer);

    if (strncmp(buffer, "STATUS:", 7) == 0)
    {
        statusTimeoutCounter = 0;

        double angle = 0;
        int brightness = 0;
        char safety[16] = {0};

        sscanf(buffer + 7, "%lf,%d,%15s", &angle, &brightness, safety);

        capIsMoving = false;
        movingCounter = 0;
        capIsParked = (angle >= 269.0);

        ParkCapSP.reset();
        ParkCapSP[capIsParked ? 0 : 1].setState(ISS_ON);
        ParkCapSP.setState(IPS_OK);
        ParkCapSP.apply();

        LightSP.reset();
        LightSP[brightness > 0 ? 0 : 1].setState(ISS_ON);
        LightSP.setState(IPS_OK);
        LightSP.apply();

        LightIntensityNP[0].setValue(brightness);
        LightIntensityNP.setState(IPS_OK);
        LightIntensityNP.apply();

        SafetyOverrideSP.reset();
        SafetyOverrideSP[strcmp(safety, "SAFE") == 0 ? 0 : 1].setState(ISS_ON);
        SafetyOverrideSP.setState(IPS_OK);
        SafetyOverrideSP.apply();
    }

    else if (strncmp(buffer, "IDLE:", 5) == 0)
    {
        double angle = atof(buffer + 5);
        capIsMoving = false;
        movingCounter = 0;
        capIsParked = (angle >= 269.0);

        ParkCapSP.reset();
        ParkCapSP[capIsParked ? 0 : 1].setState(ISS_ON);
        ParkCapSP.setState(IPS_OK);
        ParkCapSP.apply();
    }

    else if (strcmp(buffer, "MOVING") == 0)
    {
        capIsMoving = true;
        movingCounter = 0;
        ParkCapSP.setState(IPS_BUSY);
        ParkCapSP.apply();
    }

    SetTimer(capIsMoving ? 250 : 1000);
}