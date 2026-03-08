#include "dustcapino.h"
#include <termios.h>
#include <cstring>
#include <unistd.h>

static std::unique_ptr<DustCapIno> dustcapino(new DustCapIno());

DustCapIno::DustCapIno() :
    INDI::LightBoxInterface(this),
    INDI::DustCapInterface(this)
{
    setVersion(1, 3);
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

    usleep(2000000);
    tcflush(PortFD, TCIOFLUSH);

    int written = 0;
    tty_write_string(PortFD, "CMD:STATUS\n", &written);

    capState = CapState::UNKNOWN;
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

IPState DustCapIno::ParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    int written = 0;
    tty_write_string(PortFD, "CMD:CLOSE\n", &written);

    capState = CapState::MOVING;
    movingCounter = 0;
    updateParkUI();

    return IPS_BUSY;
}

IPState DustCapIno::UnParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    EnableLightBox(false);

    int written = 0;
    tty_write_string(PortFD, "CMD:OPEN\n", &written);

    capState = CapState::MOVING;
    movingCounter = 0;
    updateParkUI();

    return IPS_BUSY;
}

bool DustCapIno::autoDetectPort()
{
    int baud = 9600;
    if (BaudRateSP[1].getState() == ISS_ON) baud = 19200;
    if (BaudRateSP[2].getState() == ISS_ON) baud = 57600;
    if (BaudRateSP[3].getState() == ISS_ON) baud = 115200;

    const char* prefixes[] = {"/dev/ttyUSB", "/dev/ttyACM"};

    for (const char* prefix : prefixes)
    {
        for (int i = 0; i < 6; i++)
        {
            char port[32];
            snprintf(port, sizeof(port), "%s%d", prefix, i);

            int fd = -1;

            if (tty_connect(port, baud, 8, 0, 1, &fd) != TTY_OK)
                continue;

            usleep(2000000);
            tcflush(fd, TCIOFLUSH);

            int written = 0;
            tty_write_string(fd, "CMD:STATUS\n", &written);

            char buffer[128] = {0};
            int nbytes_read = 0;

           bool detected = false;

for (int attempt = 0; attempt < 5; attempt++)
{
    memset(buffer, 0, sizeof(buffer));

    int rc = tty_read_section(fd, buffer, '\n', 2, &nbytes_read);

    if (rc == TTY_OK && nbytes_read > 0)
    {
        buffer[strcspn(buffer, "\r\n")] = 0;

        LOGF_INFO("AutoDetect RX on %s: %s", port, buffer);

        if (strstr(buffer, "STATUS:") != nullptr ||
            strstr(buffer, "BOOTED")  != nullptr)
        {
            detected = true;
            break;
        }
    }

    usleep(200000);
}

if (detected)
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

        if (capState != CapState::CLOSED)
            EnableLightBox(false);

        CoverAngleNP.setState(IPS_OK);
        CoverAngleNP.apply();
        return true;
    }

    if (INDI::LightBoxInterface::processNumber(dev, name, values, names, n))
        return true;

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

bool DustCapIno::SetLightBoxBrightness(uint16_t value)
{
    if (PortFD <= 0)
        return false;

    bool safetyOn = (SafetyOverrideSP[0].getState() == ISS_ON);

    if (value > 0 && safetyOn && capState != CapState::CLOSED)
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

    if (enable && safetyOn && capState != CapState::CLOSED)
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

void DustCapIno::updateParkUI()
{
    ParkCapSP.reset();

    switch (capState)
    {
        case CapState::CLOSED:
            ParkCapSP[0].setState(ISS_ON);
            ParkCapSP.setState(IPS_OK);
            break;

        case CapState::OPEN:
            ParkCapSP[1].setState(ISS_ON);
            ParkCapSP.setState(IPS_OK);
            break;

        case CapState::MOVING:
            ParkCapSP.setState(IPS_BUSY);
            break;

        case CapState::ALERT:
            ParkCapSP.setState(IPS_ALERT);
            break;

        default:
            ParkCapSP.setState(IPS_IDLE);
            break;
    }

    ParkCapSP.apply();
}

void DustCapIno::TimerHit()
{
    // -------------------------------------------------
    // Not connected at INDI level
    // -------------------------------------------------
    if (!isConnected())
    {
        SetTimer(1000);
        return;
    }

    // -------------------------------------------------
    // Serial port lost → controlled reconnect
    // -------------------------------------------------
    if (PortFD <= 0)
    {
        reconnectCounter++;

        if (reconnectCounter >= reconnectInterval)
        {
            LOG_WARN("Attempting reconnect...");
            reconnectCounter = 0;

            if (autoDetectPort())
                LOG_INFO("Reconnected successfully");
        }

        SetTimer(1000);
        return;
    }

    char buffer[128] = {0};
    int nbytes_read = 0;

    int rc = tty_read_section(PortFD, buffer, '\n', 5, &nbytes_read);

    // -------------------------------------------------
    // Serial read failure
    // -------------------------------------------------
    if (rc != TTY_OK)
    {
        LOG_ERROR("Serial read error — reconnecting");

        tty_disconnect(PortFD);
        PortFD = -1;

        capState = CapState::ALERT;
        updateParkUI();

        LightSP.setState(IPS_ALERT);
        LightSP.apply();

        LightIntensityNP.setState(IPS_ALERT);
        LightIntensityNP.apply();

        movingCounter = 0;
        statusTimeoutCounter = 0;

        SetTimer(2000);
        return;
    }

    // -------------------------------------------------
    // Timeout detection
    // -------------------------------------------------
    statusTimeoutCounter++;
    if (statusTimeoutCounter > 10)
    {
        capState = CapState::ALERT;
        updateParkUI();
        statusTimeoutCounter = 0;
    }

    // -------------------------------------------------
    // Movement watchdog
    // -------------------------------------------------
    if (capState == CapState::MOVING)
    {
        movingCounter++;
        if (movingCounter > 20)
        {
            capState = CapState::ALERT;
            updateParkUI();
        }
    }

    // -------------------------------------------------
    // Nothing received → still poll
    // -------------------------------------------------
    if (nbytes_read == 0)
    {
        int written = 0;
        tty_write_string(PortFD, "CMD:STATUS\n", &written);
        tty_write_string(PortFD, "CMD:READ_DHT\n", &written);

        SetTimer(capState == CapState::MOVING ? 250 : 1000);
        return;
    }

    buffer[strcspn(buffer, "\r\n")] = 0;

    // =================================================
    // STATUS handling
    // =================================================
    if (strncmp(buffer, "STATUS:", 7) == 0)
    {
        statusTimeoutCounter = 0;
        reconnectCounter = 0;

        double angle = 0;
        int brightness = 0;
        char safety[16] = {0};

        sscanf(buffer + 7, "%lf,%d,%15s",
               &angle, &brightness, safety);

        // --- Cap state detection ---
        if (angle <= 1.0)
            capState = CapState::OPEN;
        else if (angle >= 269.0)
            capState = CapState::CLOSED;
        else
            capState = CapState::MOVING;

        movingCounter = 0;
        updateParkUI();

        // --- Light state ---
        LightSP.reset();
        LightSP[brightness > 0 ? 0 : 1].setState(ISS_ON);
        LightSP.setState(IPS_OK);
        LightSP.apply();

        LightIntensityNP[0].setValue(brightness);
        LightIntensityNP.setState(IPS_OK);
        LightIntensityNP.apply();

        // --- Safety ---
        SafetyOverrideSP.reset();
        SafetyOverrideSP[strncmp(safety, "SAFE", 4) == 0 ? 0 : 1]
            .setState(ISS_ON);
        SafetyOverrideSP.setState(IPS_OK);
        SafetyOverrideSP.apply();
    }

    // =================================================
    // IDLE handling
    // =================================================
    else if (strncmp(buffer, "IDLE:", 5) == 0)
    {
        double angle = atof(buffer + 5);

        if (angle <= 1.0)
            capState = CapState::OPEN;
        else
            capState = CapState::CLOSED;

        movingCounter = 0;
        updateParkUI();
    }

    // =================================================
    // MOVING message
    // =================================================
    else if (strcmp(buffer, "MOVING") == 0)
    {
        capState = CapState::MOVING;
        movingCounter = 0;
        updateParkUI();
    }

    // =================================================
    // DHT handling
    // =================================================
    else if (strncmp(buffer, "DHT:", 4) == 0)
    {
        double temp = 0;
        double hum  = 0;

        if (sscanf(buffer + 4, "%lf,%lf", &temp, &hum) == 2)
        {
            EnvNP[0].setValue(temp);
            EnvNP[1].setValue(hum);
            EnvNP.setState(IPS_OK);
            EnvNP.apply();
        }
    }

    // -------------------------------------------------
    // Polling
    // -------------------------------------------------
    int written = 0;
    tty_write_string(PortFD, "CMD:STATUS\n", &written);
    tty_write_string(PortFD, "CMD:READ_DHT\n", &written);

    SetTimer(capState == CapState::MOVING ? 250 : 1000);
}