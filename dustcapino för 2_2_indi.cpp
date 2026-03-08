#include "dustcapino.h"
#include <termios.h>
#include <cstring>
#include <unistd.h>

static std::unique_ptr<DustCapIno> dustcapino(new DustCapIno());

DustCapIno::DustCapIno() :
    INDI::LightBoxInterface(this),
    INDI::DustCapInterface(this)
{
    setVersion(1, 0);
    PortFD = -1;
}

const char *DustCapIno::getDefaultName()
{
    return "DustCapIno";
}

bool DustCapIno::initProperties()
{
    INDI::DefaultDevice::initProperties();

    setDriverInterface(AUX_INTERFACE | LIGHTBOX_INTERFACE | DUSTCAP_INTERFACE);
    addAuxControls();

    INDI::LightBoxInterface::initProperties("Main Control",
                                            INDI::LightBoxInterface::CAN_DIM);

    INDI::DustCapInterface::initProperties("Main Control");

    // Serial port
    SerialPortTP[0].fill("PORT", "Port", "/dev/ttyUSB2");
    SerialPortTP.fill(getDeviceName(), "DEVICE_PORT", "Serial",
                      OPTIONS_TAB, IP_RW, 60, IPS_IDLE);
    defineProperty(SerialPortTP);

    // Baud
    BaudRateSP[0].fill("9600", "9600", ISS_OFF);
    BaudRateSP[1].fill("19200", "19200", ISS_OFF);
    BaudRateSP[2].fill("57600", "57600", ISS_OFF);
    BaudRateSP[3].fill("115200", "115200", ISS_ON);

    BaudRateSP.fill(getDeviceName(), "BAUD_RATE", "Serial",
                    OPTIONS_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
    defineProperty(BaudRateSP);

    // Safety
    SafetyOverrideSP[0].fill("SAFE", "Safety ON", ISS_ON);
    SafetyOverrideSP[1].fill("OVERRIDE", "Override", ISS_OFF);

    SafetyOverrideSP.fill(getDeviceName(), "LIGHT_SAFETY",
                          "Light Safety", MAIN_CONTROL_TAB,
                          IP_RW, ISR_1OFMANY, 0, IPS_OK);
    defineProperty(SafetyOverrideSP);

    // Cover angle (manuell styrning)
    CoverAngleNP[0].fill("ANGLE", "Angle", "%.f", 0, 270, 1, 135);
    CoverAngleNP.fill(getDeviceName(), "COVER_ANGLE",
                      "Cover Position", MAIN_CONTROL_TAB,
                      IP_RW, 0, IPS_IDLE);
    defineProperty(CoverAngleNP);

    // Environment
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

    if (isConnected())
    {
        INDI::LightBoxInterface::updateProperties();
        INDI::DustCapInterface::updateProperties();
    }

    return true;
}

bool DustCapIno::Connect()
{
    const char *port = SerialPortTP[0].getText();

    int baud = 9600;
    if (BaudRateSP[1].getState() == ISS_ON) baud = 19200;
    if (BaudRateSP[2].getState() == ISS_ON) baud = 57600;
    if (BaudRateSP[3].getState() == ISS_ON) baud = 115200;

    if (tty_connect(port, baud, 8, 0, 1, &PortFD) != TTY_OK)
    {
        LOG_ERROR("Failed to open serial port");
        return false;
    }

    LOGF_INFO("Connected to %s @ %d", port, baud);

    sleep(2);
    tcflush(PortFD, TCIOFLUSH);

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

bool DustCapIno::ISNewSwitch(const char *dev,
                             const char *name,
                             ISState *states,
                             char *names[],
                             int n)
{
    if (strcmp(dev, getDeviceName()) != 0)
        return false;

    if (!strcmp(name, "LIGHT_SAFETY"))
    {
        SafetyOverrideSP.update(states, names, n);
        SafetyOverrideSP.setState(IPS_OK);
        SafetyOverrideSP.apply();
        return true;
    }

    if (INDI::LightBoxInterface::processSwitch(dev, name, states, names, n))
        return true;

    if (INDI::DustCapInterface::processSwitch(dev, name, states, names, n))
        return true;

    return INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n);
}

bool DustCapIno::ISNewNumber(const char *dev,
                             const char *name,
                             double values[],
                             char *names[],
                             int n)
{
    if (strcmp(dev, getDeviceName()) != 0)
        return false;

    if (!strcmp(name, "COVER_ANGLE"))
    {
        CoverAngleNP.update(values, names, n);
        double angle = CoverAngleNP[0].getValue();

        if (PortFD > 0)
        {
            int written = 0;
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "CMD:ANGLE:%.0f\n", angle);
            tty_write_string(PortFD, cmd, &written);
        }

        // Leaving parked → force light OFF
        if (angle != 270)
            EnableLightBox(false);

        CoverAngleNP.setState(IPS_OK);
        CoverAngleNP.apply();
        return true;
    }

    if (!strcmp(name, "FLAT_LIGHT_INTENSITY"))
    {
        bool safetyOn = (SafetyOverrideSP[0].getState() == ISS_ON);
        bool parked = capIsParked;

        if (values[0] > 0 && safetyOn && !parked)
        {
            LOG_WARN("Brightness blocked by safety (cap not parked)");
            return true;
        }
    }

    if (INDI::LightBoxInterface::processNumber(dev, name, values, names, n))
        return true;

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

IPState DustCapIno::ParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    capIsMoving = true;

    int written = 0;
    tty_write_string(PortFD, "CMD:CLOSE\n", &written);

    return IPS_BUSY;
}

IPState DustCapIno::UnParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    capIsMoving = true;

    int written = 0;
    tty_write_string(PortFD, "CMD:OPEN\n", &written);

    return IPS_BUSY;
}

IPState DustCapIno::getParkState()
{
    if (capIsMoving)
        return IPS_BUSY;

    return capIsParked ? IPS_OK : IPS_IDLE;
}

bool DustCapIno::SetLightBoxBrightness(uint16_t value)
{
    if (PortFD <= 0)
        return false;

    bool safetyOn = (SafetyOverrideSP[0].getState() == ISS_ON);
    bool parked = capIsParked;

    if (value > 0 && safetyOn && !parked)
    {
        LOG_WARN("Brightness blocked by safety (cap not parked)");
        return false;
    }

    int written = 0;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "CMD:BRIGHTNESS:%d\n", value);
    tty_write_string(PortFD, cmd, &written);

    if (value > 0)
        EnableLightBox(true);
    else
        EnableLightBox(false);

    return true;
}

bool DustCapIno::EnableLightBox(bool enable)
{
    if (PortFD <= 0)
        return false;

    bool safetyOn = (SafetyOverrideSP[0].getState() == ISS_ON);
    bool parked = capIsParked;

    if (enable && safetyOn && !parked)
    {
        LOG_WARN("Light blocked by safety");
        return false;
    }

    int written = 0;

    if (enable)
        tty_write_string(PortFD, "CMD:LIGHT_ON\n", &written);
    else
        tty_write_string(PortFD, "CMD:LIGHT_OFF\n", &written);

    return true;
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

    // ================= SERIAL ERROR =================
    if (rc != TTY_OK)
    {
        LOG_ERROR("Serial read error – disconnecting");

        tty_disconnect(PortFD);
        PortFD = -1;

        setConnected(false);
        return;
    }

    if (nbytes_read == 0)
    {
        SetTimer(1000);
        return;
    }

    buffer[strcspn(buffer, "\r\n")] = 0;
    LOGF_INFO("RX: %s", buffer);

    // ================= MOVING =================
    if (strcmp(buffer, "MOVING") == 0)
    {
        capIsMoving = true;
        ParkCapSP.setState(IPS_BUSY);
        ParkCapSP.apply();
    }

    // ================= IDLE OPEN =================
    else if (strncmp(buffer, "IDLE_OPEN", 9) == 0)
    {
        capIsParked = false;
        capIsMoving = false;

        ParkCapSP.reset();
        ParkCapSP[1].setState(ISS_ON);  // UNPARK
        ParkCapSP.setState(IPS_OK);
        ParkCapSP.apply();

        EnableLightBox(false);
    }

    // ================= IDLE CLOSED =================
    else if (strncmp(buffer, "IDLE_CLOSED", 11) == 0)
    {
        capIsParked = true;
        capIsMoving = false;

        ParkCapSP.reset();
        ParkCapSP[0].setState(ISS_ON);  // PARK
        ParkCapSP.setState(IPS_OK);
        ParkCapSP.apply();
    }

    // ================= STATUS =================
    else if (strncmp(buffer, "STATUS:", 7) == 0)
    {
        double angle = 0;
        int brightness = 0;
        char safety[16] = {0};

        sscanf(buffer + 7, "%lf,%d,%15s", &angle, &brightness, safety);

        capIsParked = (angle >= 269.0);
        capIsMoving = false;

        // Sync park
        ParkCapSP.reset();
        if (capIsParked)
            ParkCapSP[0].setState(ISS_ON);
        else
            ParkCapSP[1].setState(ISS_ON);

        ParkCapSP.setState(IPS_OK);
        ParkCapSP.apply();

        // Sync light
        LightSP.reset();
        if (brightness > 0)
            LightSP[0].setState(ISS_ON);
        else
            LightSP[1].setState(ISS_ON);

        LightSP.setState(IPS_OK);
        LightSP.apply();
    }

    SetTimer(1000);
}