#include "dustcapino.h"
#include <termios.h>
#include <cstring>
#include <unistd.h>

static std::unique_ptr<DustCapIno> dustcapino(new DustCapIno());
constexpr int POLL_FAST  = 250;
constexpr int POLL_IDLE  = 2000;
constexpr int POLL_ALERT = 3000;

DustCapIno::DustCapIno() :
    INDI::LightBoxInterface(this),
    INDI::DustCapInterface(this)
{
    setVersion(1, 99);
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
    addPollPeriodControl();
    setDefaultPollingPeriod(200);

    // --------------------------------------------------
    // Maintenance commands
    // --------------------------------------------------

    static INDI::WidgetSwitch maintenanceS[5];

MaintenanceSP[0].fill("REBOOT", "Reboot Controller", ISS_OFF);
MaintenanceSP[1].fill("RESET_WDT", "Reset Watchdog", ISS_OFF);
MaintenanceSP[2].fill("UPLOAD", "Upload Mode", ISS_OFF);
MaintenanceSP[3].fill("STATUS", "Force Status", ISS_OFF);
MaintenanceSP[4].fill("DEBUG_LOG", "Debug Logs", ISS_OFF);

MaintenanceSP.fill(getDeviceName(),
                   "MAINTENANCE",
                   "Maintenance",
                   OPTIONS_TAB,
                   IP_RW,
                   ISR_NOFMANY,
                   0,
                   IPS_IDLE);

defineProperty(MaintenanceSP);

    // --------------------------------------------------
    // Firmware info
    // --------------------------------------------------

    FirmwareInfoTP[0].fill("FW_VERSION", "Firmware", "");
    FirmwareInfoTP[1].fill("RESET_CAUSE", "Reset Cause", "");
    FirmwareInfoTP[2].fill("BUILD_DATE", "Build", "");

    FirmwareInfoTP.fill(getDeviceName(),
                        "FIRMWARE_INFO",
                        "Firmware",
                        INFO_TAB,
                        IP_RO,
                        0,
                        IPS_IDLE);

    defineProperty(FirmwareInfoTP);



    // --------------------------------------------------
    // Interfaces
    // --------------------------------------------------

    INDI::LightBoxInterface::initProperties("Main Control",
                                            INDI::LightBoxInterface::CAN_DIM);

    INDI::DustCapInterface::initProperties("Main Control");

            StatusTP[0].fill("CAP_STATE", "Cap State", "");
        StatusTP[1].fill("LIGHT_STATE", "Light", "");
        StatusTP[2].fill("SAFETY_STATE", "Safety", "");
        StatusTP[3].fill("CONNECTION", "Connection", "");

        StatusTP.fill(getDeviceName(),
                      "DEVICE_STATUS",
                      "Status",
                      INFO_TAB,
                      IP_RO,
                      0,
                      IPS_IDLE);

        defineProperty(StatusTP);

    // --------------------------------------------------
    // Serial port
    // --------------------------------------------------

    SerialPortTP[0].fill("PORT", "Port", "AUTO");

    SerialPortTP.fill(getDeviceName(),
                      "DEVICE_PORT",
                      "Serial",
                      OPTIONS_TAB,
                      IP_RW,
                      60,
                      IPS_IDLE);

    defineProperty(SerialPortTP);

    // --------------------------------------------------
    // Baud rate
    // --------------------------------------------------

    BaudRateSP[0].fill("9600", "9600", ISS_OFF);
    BaudRateSP[1].fill("19200", "19200", ISS_OFF);
    BaudRateSP[2].fill("57600", "57600", ISS_OFF);
    BaudRateSP[3].fill("115200", "115200", ISS_ON);

    BaudRateSP.fill(getDeviceName(),
                    "BAUD_RATE",
                    "Serial",
                    OPTIONS_TAB,
                    IP_RW,
                    ISR_1OFMANY,
                    0,
                    IPS_IDLE);

    defineProperty(BaudRateSP);

    // --------------------------------------------------
    // Light safety
    // --------------------------------------------------

    SafetyOverrideSP[0].fill("SAFE", "Safety ON", ISS_ON);
    SafetyOverrideSP[1].fill("OVERRIDE", "Override", ISS_OFF);

    SafetyOverrideSP.fill(getDeviceName(),
                          "LIGHT_SAFETY",
                          "Light Safety",
                          MAIN_CONTROL_TAB,
                          IP_RW,
                          ISR_1OFMANY,
                          0,
                          IPS_OK);

    defineProperty(SafetyOverrideSP);

    // --------------------------------------------------
    // Cover angle control
    // --------------------------------------------------

    CoverAngleNP[0].fill("ANGLE", "Angle", "%.f", 0, 270, 1, 135);

    CoverAngleNP.fill(getDeviceName(),
                      "COVER_ANGLE",
                      "Cover Position",
                      MAIN_CONTROL_TAB,
                      IP_RW,
                      0,
                      IPS_IDLE);

    defineProperty(CoverAngleNP);

    // --------------------------------------------------
    // Environment sensor
    // --------------------------------------------------

    EnvNP[0].fill("TEMPERATURE", "Temperature (C)", "%.2f", -40, 80, 0, 0);
    EnvNP[1].fill("HUMIDITY", "Humidity (%)", "%.2f", 0, 100, 0, 0);

    EnvNP.fill(getDeviceName(),
               "DHT_ENV",
               "Environment",
               MAIN_CONTROL_TAB,
               IP_RO,
               0,
               IPS_IDLE);

    defineProperty(EnvNP);
    CoverProgressNP[0].fill("PROGRESS",
                            "Open %",
                            "%.0f %%",   // <-- viktigt
                            0,
                            100,
                            1,
                            0);

    CoverProgressNP.fill(getDeviceName(),
                         "COVER_PROGRESS",
                         "Cover Progress",
                         MAIN_CONTROL_TAB,
                         IP_RO,
                         0,
                         IPS_IDLE);

    defineProperty(CoverProgressNP);
    // --------------------------------------------------
    // Handshake info
    // --------------------------------------------------

    HandshakeTP[0].fill("HANDSHAKE_STATUS",
                        "Handshake Status",
                        "Not connected");

    HandshakeTP.fill(getDeviceName(),
                     "HANDSHAKE_INFO",
                     "Handshake",
                     INFO_TAB,
                     IP_RO,
                     0,
                     IPS_IDLE);

    defineProperty(HandshakeTP);

    HealthNP[0].fill("VOLTAGE", "Arduino V", "%.2f", 0, 6, 0, 0);
    HealthNP[1].fill("RAM", "Free RAM", "%.0f", 0, 2000, 0, 0);
    HealthNP[2].fill("SERVO_PULSE", "Servo Pulse", "%.0f", 0, 2500, 0, 0);
    HealthNP[3].fill("MOVING", "Moving", "%.0f", 0, 1, 0, 0);

    HealthNP.fill(getDeviceName(),
                  "DEVICE_HEALTH",
                  "Diagnostics",
                  OPTIONS_TAB,
                  IP_RO,
                  0,
                  IPS_IDLE);

    defineProperty(HealthNP);

    return true;
}

bool DustCapIno::updateProperties()
{
    if (!INDI::DefaultDevice::updateProperties())
        return false;

    INDI::LightBoxInterface::updateProperties();
    INDI::DustCapInterface::updateProperties();

    if (isConnected())
    {
        // 🔥 Ladda sparad config
        loadConfig(true);
        LOG_INFO("Configuration loaded");
    }

    return true;
}

bool DustCapIno::Connect()
{
    int baud = 9600;
    if (BaudRateSP[1].getState() == ISS_ON) baud = 19200;
    if (BaudRateSP[2].getState() == ISS_ON) baud = 57600;
    if (BaudRateSP[3].getState() == ISS_ON) baud = 115200;

    const char *port = SerialPortTP[0].getText();


if (strcmp(port, "AUTO") != 0)
{
    if (tty_connect(port, baud, 8, 0, 1, &PortFD) == TTY_OK)
    {
        LOGF_INFO("Connected using saved port %s", port);
        return true;
    }

    LOGF_WARN("Saved port %s failed, falling back to auto detect", port);
}

// AUTO eller fallback
if (!autoDetectPort())
{
    LOG_ERROR("Auto detect failed");
    return false;
}

tcflush(PortFD, TCIOFLUSH);

int written = 0;
tty_write_string(PortFD, "CMD:HELLO\n", &written);

char buffer[128] = {0};
int nbytes_read = 0;

if (tty_read_section(PortFD, buffer, '\n', 1, &nbytes_read) == TTY_OK)
{
    buffer[strcspn(buffer, "\r\n")] = 0;

    if (strstr(buffer, "DUSTCAPINO") != nullptr)
    {
        firmwareVersion = buffer;
    }
}

tty_write_string(PortFD, "CMD:LOG_DEBUG\n", &written);

capState = CapState::UNKNOWN;

tty_write_string(PortFD, "CMD:STATUS\n", &written);

SetTimer(200);

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
    LOG_INFO("Dust cap parking (closing)");

    if (PortFD <= 0)
        return IPS_ALERT;

    int written = 0;
    lastMove = MOVE_CLOSE;

    tty_write_string(PortFD, "CMD:CLOSE\n", &written);
    tcdrain(PortFD);

    capState = CapState::MOVING;

    return IPS_BUSY;
}

IPState DustCapIno::UnParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    EnableLightBox(false);

    int written = 0;
    tty_write_string(PortFD, "CMD:OPEN\n", &written);
    lastMove = MOVE_OPEN;

    capState = CapState::MOVING;
    movingCounter = 0;

    return IPS_BUSY;  // <-- ENDASTE detta
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
        for (int i = 0; i < 3; i++)
        {
            char port[32];
            snprintf(port, sizeof(port), "%s%d", prefix, i);

            int fd = -1;

            if (tty_connect(port, baud, 8, 0, 1, &fd) != TTY_OK)
                continue;

            // Vänta på Arduino boot
            usleep(400000);
            tcflush(fd, TCIOFLUSH);

            int written = 0;



            char buffer[128] = {0};
            int nbytes_read = 0;

            bool detected = false;
bool timeoutOccurred = true;
bool wrongFirmware = false;

tty_write_string(fd, "CMD:HELLO\n", &written);

for (int attempt = 0; attempt < 5; attempt++)
{
    memset(buffer, 0, sizeof(buffer));

    int rc = tty_read_section(fd, buffer, '\n', 0.5, &nbytes_read);

    if (rc == TTY_OK && nbytes_read > 0)
    {
        timeoutOccurred = false;

        buffer[strcspn(buffer, "\r\n")] = 0;

        LOGF_INFO("Handshake RX on %s: %s", port, buffer);

      if (strstr(buffer, "HELLO:DUSTCAPINO") != nullptr ||
    strstr(buffer, "DUSTCAPINO_V1.") != nullptr)
{
    firmwareVersion = buffer;
    detected = true;
    break;
}
else
{
    wrongFirmware = true;
}
    }

    usleep(200000);
}

if (detected)
{
    LOGF_INFO("DustCapIno detected via handshake on %s", port);

    bool wasDisconnected = (PortFD <= 0);

    PortFD = fd;

    SerialPortTP[0].setText(port);
    SerialPortTP.setState(IPS_OK);
    SerialPortTP.apply();
saveConfig();


    // Handshake OK
    HandshakeTP[0].setText("Handshake OK");
    HandshakeTP.setState(IPS_OK);
    HandshakeTP.apply();

    // 🔥 Restore cached UI only on reconnect
    if (wasDisconnected)
    {
        LOG_INFO("Restoring cached state after reconnect");

        if (lastAngle <= 1.0)
    capState = CapState::OPEN;
else if (lastAngle >= 269.0)
    capState = CapState::CLOSED;
else
    capState = CapState::OPEN;

// 🔥 SÄTT GILTIGT STARTLÄGE FÖR CAP_PARK
ParkCapSP.reset();

if (capState == CapState::OPEN)
{
    ParkCapSP[1].setState(ISS_ON);  // UNPARK
}
else
{
    ParkCapSP[0].setState(ISS_ON);  // PARK
}

ParkCapSP.setState(IPS_OK);
ParkCapSP.apply();


        LightSP.reset();
        LightSP[lastBrightness > 0 ? 0 : 1].setState(ISS_ON);
        LightSP.setState(IPS_OK);
        LightSP.apply();

        LightIntensityNP[0].setValue(lastBrightness);
        LightIntensityNP.setState(IPS_OK);
        LightIntensityNP.apply();

        SafetyOverrideSP.reset();
        SafetyOverrideSP[lastSafe ? 0 : 1].setState(ISS_ON);
        SafetyOverrideSP.setState(IPS_OK);
        SafetyOverrideSP.apply();

        EnvNP[0].setValue(lastTemp);
        EnvNP[1].setValue(lastHum);
        EnvNP.setState(IPS_OK);
        EnvNP.apply();
    }

    return true;
}
else
{
    if (timeoutOccurred)
    {
        HandshakeTP[0].setText("No response (timeout)");
    }
    else if (wrongFirmware)
    {
        HandshakeTP[0].setText("Wrong firmware detected");
    }
    else
    {
        HandshakeTP[0].setText("Unknown device");
    }

    HandshakeTP.setState(IPS_ALERT);
    HandshakeTP.apply();
}

            tty_disconnect(fd);
        }
    }

    LOG_ERROR("Auto-detect failed (no valid handshake)");
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

    // --------------------------------------------------
    // CONFIG SAVE / LOAD
    // --------------------------------------------------
    if (!strcmp(name, "CONFIG_PROCESS"))
    {
        bool rc = INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n);

        const char *onSwitch = IUFindOnSwitchName(states, names, n);
        if (onSwitch && !strcmp(onSwitch, "CONFIG_SAVE"))
        {
            saveConfig();
            LOG_INFO("Configuration saved manually");
        }

        return rc;
    }

    // --------------------------------------------------
    // MAINTENANCE COMMANDS
    // --------------------------------------------------
    if (!strcmp(name, "MAINTENANCE"))
    {
        MaintenanceSP.update(states, names, n);

        int written = 0;

        if (MaintenanceSP[0].getState() == ISS_ON)
        {
            LOG_WARN("Reboot requested");
            tty_write_string(PortFD, "CMD:REBOOT\n", &written);
        }
        else if (MaintenanceSP[1].getState() == ISS_ON)
        {
            LOG_INFO("Reset watchdog");
            tty_write_string(PortFD, "CMD:RESET_WATCHDOG\n", &written);
        }
        else if (MaintenanceSP[2].getState() == ISS_ON)
        {
            LOG_WARN("Entering upload mode");
            tty_write_string(PortFD, "CMD:UPLOAD_MODE\n", &written);
        }
        else if (MaintenanceSP[3].getState() == ISS_ON)
        {
            LOG_INFO("Manual status request");
            tty_write_string(PortFD, "CMD:STATUS\n", &written);
        }
        else if (MaintenanceSP[4].getState() == ISS_ON)
        {
            LOG_INFO("Enable debug logging");
            tty_write_string(PortFD, "CMD:LOG_DEBUG\n", &written);
        }

        MaintenanceSP.reset();
        MaintenanceSP.setState(IPS_OK);
        MaintenanceSP.apply();

        return true;
    }

    // --------------------------------------------------
    // LIGHT SAFETY
    // --------------------------------------------------
    if (!strcmp(name, "LIGHT_SAFETY"))
    {
        SafetyOverrideSP.update(states, names, n);
        SafetyOverrideSP.setState(IPS_OK);
        SafetyOverrideSP.apply();
        return true;
    }

    // --------------------------------------------------
    // LIGHTBOX INTERFACE
    // --------------------------------------------------
    if (INDI::LightBoxInterface::processSwitch(dev, name, states, names, n))
        return true;

    // --------------------------------------------------
    // DUSTCAP INTERFACE
    // --------------------------------------------------
    if (INDI::DustCapInterface::processSwitch(dev, name, states, names, n))
        return true;

    // --------------------------------------------------
    // DEFAULT HANDLER
    // --------------------------------------------------
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

    bool isClosed = (capState == CapState::CLOSED);

if (value > 0 && safetyOn && !isClosed)
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

    bool isClosed = (capState == CapState::CLOSED);

if (enable && safetyOn && !isClosed)
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
    // --------------------------------------------------
    // Not connected to INDI
    // --------------------------------------------------
    if (!isConnected())
    {
        SetTimer(200);
        return;
    }

    // --------------------------------------------------
    // Serial reconnect handling
    // --------------------------------------------------
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

    // --------------------------------------------------
    // Read ALL serial data
    // --------------------------------------------------
    while (true)
    {
        int rc = tty_read_section(PortFD, buffer, '\n', 0.01, &nbytes_read);

        if (rc != TTY_OK && rc != TTY_TIME_OUT)
        {
            LOG_WARN("Serial error, forcing reconnect");

            tty_disconnect(PortFD);
            PortFD = -1;

            reconnectCounter = reconnectInterval;
            SetTimer(200);
            return;
        }

        if (rc == TTY_TIME_OUT || nbytes_read <= 0)
            break;

        buffer[strcspn(buffer, "\r\n")] = 0;

        // ================= HELLO =================
        if (strncmp(buffer, "HELLO:", 6) == 0)
        {
            char device[32];
            char version[32];
            char reset[32];
            char build[64];

            sscanf(buffer,
                   "HELLO:%31[^,],%31[^,],%31[^,],%*[^,],%*[^,],%63[^\n]",
                   device, version, reset, build);

            FirmwareInfoTP[0].setText(version);
            FirmwareInfoTP[1].setText(reset);
            FirmwareInfoTP[2].setText(build);

            FirmwareInfoTP.setState(IPS_OK);
            FirmwareInfoTP.apply();

            LOGF_INFO("Firmware detected: %s (%s)", version, build);
        }

       // ================= STATUS =================
            else if (strncmp(buffer, "STATUS:", 7) == 0)
            {
                statusTimeoutCounter = 0;
                reconnectCounter = 0;

                char state[20] = {0};
                double angle = 0;
                int brightness = 0;
                char safety[16] = {0};

                sscanf(buffer + 7, "%19[^,],%lf,%d,%15s",
                       state, &angle, &brightness, safety);
                bool safeNow = (strcmp(safety, "SAFE") == 0);
                // ---- Status UI ----
                StatusTP[0].setText(state);
                StatusTP[1].setText(brightness > 0 ? "ON" : "OFF");
                StatusTP[2].setText(safeNow ? "SAFE" : "OVERRIDE");
                StatusTP[3].setText("CONNECTED");

                StatusTP.setState(IPS_OK);
                StatusTP.apply();

    // ---- Angle update ----
            if (capState == CapState::MOVING || fabs(lastAngle - angle) > 0.1)
            {
                CoverAngleNP[0].setValue(angle);
                CoverAngleNP.setState(IPS_OK);
                CoverAngleNP.apply();
                lastAngle = angle;
            }

            double progress = (angle / 270.0) * 100.0;

            // stabilisera endpoints
            if (capState == CapState::CLOSED)
                progress = 0;

            if (capState == CapState::OPEN)
                progress = 100;

            CoverProgressNP[0].setValue(progress);

            if (capState == CapState::MOVING)
                CoverProgressNP.setState(IPS_BUSY);
            else
                CoverProgressNP.setState(IPS_OK);

            CoverProgressNP.apply();

            CapState previousState = capState;

            if (capState == CapState::OPEN && !lastSafe)
            {
                LOG_INFO("Auto re-arming light safety");

                SafetyOverrideSP.reset();
                SafetyOverrideSP[0].setState(ISS_ON);  // SAFE

                SafetyOverrideSP.setState(IPS_OK);
                SafetyOverrideSP.apply();

                lastSafe = true;
            }
            else if (strcmp(state, "IDLE_CLOSED") == 0)
                capState = CapState::CLOSED;
            else if (strcmp(state, "MOVING") == 0)
                capState = CapState::MOVING;
            else
                capState = CapState::ALERT;

            bool movementFinished =
                (previousState == CapState::MOVING &&
                 (capState == CapState::OPEN || capState == CapState::CLOSED));

            // ---- Update cap UI ----
            switch (capState)
            {
                case CapState::OPEN:
                    ParkCapSP.reset();
                    ParkCapSP[1].setState(ISS_ON);
                    ParkCapSP.setState(IPS_OK);
                    break;

                case CapState::CLOSED:
                    ParkCapSP.reset();
                    ParkCapSP[0].setState(ISS_ON);
                    ParkCapSP.setState(IPS_OK);
                    break;

                case CapState::MOVING:

                    ParkCapSP.reset();

                    if (lastMove == MOVE_OPEN)
                        ParkCapSP[1].setState(ISS_ON);   // UNPARK
                    else if (lastMove == MOVE_CLOSE)
                        ParkCapSP[0].setState(ISS_ON);   // PARK

                    ParkCapSP.setState(IPS_BUSY);
                    break;

                case CapState::ALERT:
                    ParkCapSP.setState(IPS_ALERT);
                    break;

                default:
                    break;
            }

            ParkCapSP.apply();

            movingCounter = 0;

            // ---- Light update ----
            if (brightness != LightIntensityNP[0].getValue())
            {
                LightSP.reset();
                LightSP[brightness > 0 ? 0 : 1].setState(ISS_ON);
                LightSP.setState(IPS_OK);
                LightSP.apply();

                LightIntensityNP[0].setValue(brightness);
                LightIntensityNP.setState(IPS_OK);
                LightIntensityNP.apply();
            }

            // ---- Safety update ----
            if (safeNow != lastSafe)
            {
                lastSafe = safeNow;

                SafetyOverrideSP.reset();
                SafetyOverrideSP[safeNow ? 0 : 1].setState(ISS_ON);
                SafetyOverrideSP.setState(IPS_OK);
                SafetyOverrideSP.apply();
            }

            if (movementFinished)
            {
                int written = 0;
                tty_write_string(PortFD, "CMD:STATUS\n", &written);
            }
        }

        // ================= DHT =================
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

                lastTemp = temp;
                lastHum  = hum;
            }
        }
        else if (strncmp(buffer, "DBG ", 4) == 0)
        {
            int pulse = 0;
            int vcc = 0;
            int ram = 0;
            int moving = 0;

            char *token = strtok(buffer + 4, " ");

            while (token != nullptr)
            {
                if (strncmp(token, "pulse=", 6) == 0)
                    pulse = atoi(token + 6);

                else if (strncmp(token, "VCC=", 4) == 0)
                    vcc = atoi(token + 4);

                else if (strncmp(token, "RAM=", 4) == 0)
                    ram = atoi(token + 4);

                else if (strncmp(token, "moving=", 7) == 0)
                    moving = atoi(token + 7);

                token = strtok(nullptr, " ");
            }

            if (vcc > 0)
            {
                HealthNP[0].setValue(vcc / 1000.0);
                HealthNP[1].setValue(ram);
                HealthNP[2].setValue(pulse);
                HealthNP[3].setValue(moving);

                HealthNP.setState(IPS_OK);
                HealthNP.apply();
            }
        }
        else if (strcmp(buffer, "FAILSAFE_CLOSE") == 0)
        {
            LOG_WARN("Fail-safe closing cap (host timeout)");
        }
        else if (strcmp(buffer, "DHT:ERROR") == 0)
        {
            EnvNP.setState(IPS_ALERT);
            EnvNP.apply();
        }
        else if (strcmp(buffer, "SERVO_STALL") == 0)
{
    LOG_WARN("Servo stall detected");

    HealthNP.setState(IPS_ALERT);
    HealthNP.apply();
}

else if (strcmp(buffer, "SERVO_POWER_FAIL") == 0)
{
    LOG_WARN("Servo power failure detected");

    HealthNP.setState(IPS_ALERT);
    HealthNP.apply();
}

else if (strcmp(buffer, "MOVE_TIMEOUT") == 0)
{
    LOG_WARN("Servo movement timeout");

    HealthNP.setState(IPS_ALERT);
    HealthNP.apply();
}
    }

    // --------------------------------------------------
    // Movement retry protection
    // --------------------------------------------------
    if (capState == CapState::MOVING)
    {
        movingCounter++;

        if (movingCounter > 20)
        {
            LOG_WARN("Movement timeout, resending command");

            int written = 0;

            if (lastMove == MOVE_CLOSE)
                tty_write_string(PortFD, "CMD:CLOSE\n", &written);
            else if (lastMove == MOVE_OPEN)
                tty_write_string(PortFD, "CMD:OPEN\n", &written);

            tty_write_string(PortFD, "CMD:STATUS\n", &written);

            movingCounter = 0;
        }
    }

    if (movingCounter > 40)
    {
        LOG_WARN("Forcing IDLE recovery");
        capState = CapState::UNKNOWN;
        movingCounter = 0;
    }

    // --------------------------------------------------
    // Smart polling
    // --------------------------------------------------
    int interval = 1000;
    int written  = 0;

    if (capState == CapState::MOVING)
        interval = 100;      // snabb UI under rörelse
    else if (capState == CapState::ALERT)
        interval = 2000;     // långsam i error
    else
        interval = 1500;     // idle

    tty_write_string(PortFD, "CMD:STATUS\n", &written);

    static time_t lastDHT = 0;
    time_t now = time(nullptr);

    if (now - lastDHT > 10)
    {
        tty_write_string(PortFD, "CMD:READ_DHT\n", &written);
        lastDHT = now;
    }

    SetTimer(interval);
}