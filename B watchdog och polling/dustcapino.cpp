#include "dustcapino.h"
#include <termios.h>
#include <cstring>
#include <unistd.h>

static std::unique_ptr<DustCapIno> dustcapino(new DustCapIno());

DustCapIno::DustCapIno() :
    INDI::LightBoxInterface(this),
    INDI::DustCapInterface(this)
{
    setVersion(1, 1);
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

    // Serial Port
    SerialPortTP[0].fill("PORT", "Port", "/dev/ttyUSB2");
    SerialPortTP.fill(getDeviceName(), "DEVICE_PORT", "Serial",
                      OPTIONS_TAB, IP_RW, 60, IPS_IDLE);
    defineProperty(SerialPortTP);

    // Baud Rate
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

    // Cover angle
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

    INDI::LightBoxInterface::updateProperties();
    INDI::DustCapInterface::updateProperties();

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
        LOGF_ERROR("Failed to open %s", port);
        return false;
    }

    LOGF_INFO("Connected to %s @ %d", port, baud);

    usleep(2000000);  // Arduino reset delay
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

        if (angle != 270)
            EnableLightBox(false);

        CoverAngleNP.setState(IPS_OK);
        CoverAngleNP.apply();
        return true;
    }

    if (INDI::LightBoxInterface::processNumber(dev, name, values, names, n))
        return true;

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

IPState DustCapIno::ParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    if (capIsParked && !capIsMoving)
        return IPS_OK;

    if (!capIsMoving)
    {
        int written = 0;
        tty_write_string(PortFD, "CMD:CLOSE\n", &written);
        capIsMoving = true;
        return IPS_BUSY;
    }

    return IPS_BUSY;
}

IPState DustCapIno::UnParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    if (!capIsParked && !capIsMoving)
        return IPS_OK;

    if (!capIsMoving)
    {
        EnableLightBox(false);

        int written = 0;
        tty_write_string(PortFD, "CMD:OPEN\n", &written);

        capIsMoving = true;
        return IPS_BUSY;
    }

    return IPS_BUSY;
}

bool DustCapIno::SetLightBoxBrightness(uint16_t value)
{
    if (PortFD <= 0)
        return false;

    bool safetyOn = (SafetyOverrideSP[0].getState() == ISS_ON);

    if (value > 0 && safetyOn && !capIsParked)
    {
        LOG_WARN("Brightness blocked by safety (cap not parked)");

        // Återställ UI
        LightIntensityNP[0].setValue(0);
        LightIntensityNP.setState(IPS_OK);
        LightIntensityNP.apply();

        LightSP.reset();
        LightSP[1].setState(ISS_ON);
        LightSP.setState(IPS_OK);
        LightSP.apply();

        return false;
    }

    int written = 0;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "CMD:BRIGHTNESS:%d\n", value);
    tty_write_string(PortFD, cmd, &written);

    return EnableLightBox(value > 0);
}

bool DustCapIno::EnableLightBox(bool enable)
{
    if (PortFD <= 0)
        return false;

    bool safetyOn = (SafetyOverrideSP[0].getState() == ISS_ON);

    if (enable && safetyOn && !capIsParked)
    {
        LOG_WARN("Light blocked by safety");
        return false;
    }

    int written = 0;

    if (enable)
    {
        tty_write_string(PortFD, "CMD:LIGHT_ON\n", &written);

        LightSP.reset();
        LightSP[0].setState(ISS_ON);
    }
    else
    {
        tty_write_string(PortFD, "CMD:LIGHT_OFF\n", &written);

        LightSP.reset();
        LightSP[1].setState(ISS_ON);
        LightIntensityNP[0].setValue(0);
        LightIntensityNP.apply();
    }

    LightSP.setState(IPS_OK);
    LightSP.apply();

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
        LOG_ERROR("Serial read error – entering ALERT state");

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

    // ================= STATUS WATCHDOG =================
    statusTimeoutCounter++;

    if (statusTimeoutCounter > 10)  // ~10 seconds without STATUS
    {
        LOG_WARN("No STATUS received – forcing ALERT");

        ParkCapSP.setState(IPS_ALERT);
        ParkCapSP.apply();

        statusTimeoutCounter = 0;
    }

    // ================= MOVING WATCHDOG =================
    if (capIsMoving)
    {
        movingCounter++;

        if (movingCounter > 20)   // 20 seconds moving
        {
            LOG_ERROR("MOVING timeout – forcing ALERT");

            capIsMoving = false;
            movingCounter = 0;

            ParkCapSP.setState(IPS_ALERT);
            ParkCapSP.apply();

            SetTimer(1000);
            return;
        }
    }

    if (nbytes_read == 0)
    {
        // Adaptive polling
        if (capIsMoving)
            SetTimer(250);
        else
            SetTimer(1000);

        return;
    }

    buffer[strcspn(buffer, "\r\n")] = 0;
    LOGF_INFO("RX: %s", buffer);

    // ================= STATUS =================
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
        if (capIsParked)
            ParkCapSP[0].setState(ISS_ON);
        else
            ParkCapSP[1].setState(ISS_ON);

        ParkCapSP.setState(IPS_OK);
        ParkCapSP.apply();

        LightSP.reset();
        if (brightness > 0)
            LightSP[0].setState(ISS_ON);
        else
            LightSP[1].setState(ISS_ON);

        LightSP.setState(IPS_OK);
        LightSP.apply();

        LightIntensityNP[0].setValue(brightness);
        LightIntensityNP.setState(IPS_OK);
        LightIntensityNP.apply();

        SafetyOverrideSP.reset();
        if (strcmp(safety, "SAFE") == 0)
            SafetyOverrideSP[0].setState(ISS_ON);
        else
            SafetyOverrideSP[1].setState(ISS_ON);

        SafetyOverrideSP.setState(IPS_OK);
        SafetyOverrideSP.apply();

        LOG_INFO("STATUS synchronized");
    }

    // ================= IDLE =================
    else if (strncmp(buffer, "IDLE:", 5) == 0)
    {
        double angle = atof(buffer + 5);

        capIsMoving = false;
        movingCounter = 0;
        capIsParked = (angle >= 269.0);

        ParkCapSP.reset();
        if (capIsParked)
            ParkCapSP[0].setState(ISS_ON);
        else
            ParkCapSP[1].setState(ISS_ON);

        ParkCapSP.setState(IPS_OK);
        ParkCapSP.apply();
    }

    // ================= MOVING =================
    else if (strcmp(buffer, "MOVING") == 0)
    {
        capIsMoving = true;
        movingCounter = 0;

        ParkCapSP.setState(IPS_BUSY);
        ParkCapSP.apply();
    }

    // Adaptive polling
    if (capIsMoving)
        SetTimer(250);
    else
        SetTimer(1000);
}