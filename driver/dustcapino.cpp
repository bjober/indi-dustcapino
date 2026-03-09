/*
===============================================================================
 DustCapIno INDI Driver
===============================================================================

 Author
    Björn Bergman

 Description
    INDI driver for the DustCapIno observatory controller.

    The driver communicates with the DustCapIno Arduino firmware via
    a serial protocol and provides control of:

        • Motorized telescope dust cap
        • Flat-field illumination panel
        • Environmental sensors (DHT22)
        • Device diagnostics

 Supported INDI Interfaces
        AUX_INTERFACE
        LIGHTBOX_INTERFACE
        DUSTCAP_INTERFACE

 Firmware Compatibility
        DustCapIno firmware v1.4+

===============================================================================
*/
//=============================================================================
// SECTION 1 — INCLUDES
//=============================================================================

#include "dustcapino.h"
#include <termios.h>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <sys/stat.h>

//=============================================================================
// SECTION 2 — DRIVER INSTANCE
//=============================================================================

static std::unique_ptr<DustCapIno> dustcapino(new DustCapIno());


//=============================================================================
// SECTION 3 — DRIVER CONSTANTS
//=============================================================================

constexpr int POLL_FAST  = 250;
constexpr int POLL_IDLE  = 2000;
constexpr int POLL_ALERT = 3000;

//=============================================================================
// SECTION 4 — DRIVER CONSTRUCTOR
//=============================================================================

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

    CoverAngleNP[0].fill("ANGLE", "Open %", "%.0f %%", 0, 100, 1, 100);

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

    HealthNP[0].fill("VOLTAGE", "Controller Voltage (V)", "%.2f", 0, 6, 0, 0);
    HealthNP[1].fill("RAM", "Free RAM", "%.0f", 0, 2000, 0, 0);
    HealthNP[2].fill("SERVO_PULSE", "Servo PWM (us)", "%.0f", 0, 2500, 0, 0);
    HealthNP[3].fill("MOVING", "Servo Moving", "%.0f", 0, 1, 0, 0);

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
    // Disable Arduino auto-reset
    int flags = TIOCM_DTR;
    ioctl(PortFD, TIOCMBIC, &flags);

    LOGF_INFO("Connected using saved port %s", port);
    return true;
}
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

    lastMove = MOVE_CLOSE;

    sendCommand("CMD:CLOSE\n");

    CoverAngleNP[0].setValue(0);
    CoverAngleNP.setState(IPS_BUSY);
    CoverAngleNP.apply();
    tcdrain(PortFD);

    capState = CapState::MOVING;
    movingCounter = 0;

    return IPS_BUSY;
}

IPState DustCapIno::UnParkCap()
{
    if (PortFD <= 0)
        return IPS_ALERT;

    EnableLightBox(false);

    sendCommand("CMD:OPEN\n");

    CoverAngleNP[0].setValue(100);
    CoverAngleNP.setState(IPS_BUSY);
    CoverAngleNP.apply();
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

            usleep(200000);
            tcflush(fd, TCIOFLUSH);

            int written = 0;
            tty_write_string(fd, "CMD:HELLO\n", &written);

            char buffer[128] = {0};
            int nbytes_read = 0;

            bool detected = false;

            for (int attempt = 0; attempt < 3; attempt++)
            {
                memset(buffer, 0, sizeof(buffer));

                int rc = tty_read_section(fd, buffer, '\n', 0.3, &nbytes_read);

                if (rc == TTY_OK && nbytes_read > 0)
                {
                    buffer[strcspn(buffer, "\r\n")] = 0;

                    LOGF_INFO("Handshake RX on %s: %s", port, buffer);

                    if (strstr(buffer, "HELLO:DUSTCAPINO") != nullptr ||
                        strstr(buffer, "DUSTCAPINO_V1.") != nullptr)
                    {
                        firmwareVersion = buffer;
                        detected = true;
                        break;
                    }
                }

                usleep(100000);
            }

            if (detected)
            {
                LOGF_INFO("DustCapIno detected on %s", port);

                PortFD = fd;

                SerialPortTP[0].setText(port);
                SerialPortTP.setState(IPS_OK);
                SerialPortTP.apply();

                saveConfig();

                HandshakeTP[0].setText("Handshake OK");
                HandshakeTP.setState(IPS_OK);
                HandshakeTP.apply();

                // Force immediate state sync
                sendCommand("CMD:STATUS\n");

                // Prevent immediate duplicate STATUS from TimerHit
                lastStatusRequest = time(nullptr);

                return true;
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

        if (MaintenanceSP[0].getState() == ISS_ON)
        {
            LOG_WARN("Reboot requested");
            sendCommand("CMD:REBOOT\n");
        }
        else if (MaintenanceSP[1].getState() == ISS_ON)
        {
            LOG_INFO("Reset watchdog");
            sendCommand("CMD:RESET_WATCHDOG\n");
        }
        else if (MaintenanceSP[2].getState() == ISS_ON)
        {
            LOG_WARN("Entering upload mode");
            sendCommand("CMD:UPLOAD_MODE\n");
        }
        else if (MaintenanceSP[3].getState() == ISS_ON)
        {
            LOG_INFO("Manual status request");
            sendCommand("CMD:STATUS\n");
        }
        else if (MaintenanceSP[4].getState() == ISS_ON)
        {
            LOG_INFO("Enable debug logging");
            sendCommand("CMD:LOG_DEBUG\n");
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

        if (SafetyOverrideSP[0].getState() == ISS_ON)
        {
            sendCommand("CMD:SAFE\n");
            LOG_INFO("Light safety enabled");
        }
        else
        {
            sendCommand("CMD:OVERRIDE\n");
            LOG_WARN("Light safety override enabled");
        }

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

        double percent = CoverAngleNP[0].getValue();
        double angle = (100 - percent) * 2.7;

        if (PortFD > 0)
        {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "CMD:ANGLE:%.0f\n", angle);
            sendCommand(cmd);
        }

        if (capState != CapState::CLOSED)
            EnableLightBox(false);

        capState = CapState::MOVING;

        CoverAngleNP.setState(IPS_BUSY);
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
    EnableLightBox(value > 0);

    snprintf(cmd, sizeof(cmd), "CMD:BRIGHTNESS,%d\n", value);
    tty_write_string(PortFD, cmd, &written);

    return true;
}

bool DustCapIno::EnableLightBox(bool enable)
{
    if (PortFD <= 0)
        return false;

    bool safetyOn = (SafetyOverrideSP[0].getState() == ISS_ON);

    bool isClosed = (capState == CapState::CLOSED);

if (enable && safetyOn && !isClosed)
{
    LOG_WARN("Light blocked — cap not closed");

    LightSP.reset();
    LightSP[1].setState(ISS_ON);
    LightSP.setState(IPS_OK);
    LightSP.apply();

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

void DustCapIno::sendCommand(const char *cmd)
{
    if (PortFD <= 0)
        return;
    if (strncmp(cmd, "CMD:STATUS", 10) != 0 &&
        strncmp(cmd, "CMD:READ_DHT", 12) != 0)
    {
        LOGF_INFO("TX -> %s", cmd);
    }
    int written = 0;
    tty_write_string(PortFD, cmd, &written);
}

bool DustCapIno::readLine(char *buffer, int &nbytes)
{
    int rc = tty_read_section(PortFD, buffer, '\n', 0.1, &nbytes);

    if (rc == TTY_TIME_OUT || nbytes <= 0)
        return false;

    if (rc != TTY_OK)
        return false;

    buffer[strcspn(buffer, "\r\n")] = 0;
    return true;
}

void DustCapIno::dispatchPacket(char *buffer)
{
    if (strncmp(buffer, "HELLO:", 6) == 0)
    {
        parseHello(buffer);
    }
    else if (strncmp(buffer, "STATUS:", 7) == 0)
    {
        parseStatus(buffer);
    }
    else if (strncmp(buffer, "DHT:", 4) == 0)
    {
        parseDHT(buffer);
    }
    else if (strncmp(buffer, "DBG ", 4) == 0)
    {
        parseDiagnostics(buffer);
    }
    else
    {
        parseErrors(buffer);
    }
}

void DustCapIno::parseHello(const char *buffer)
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

    // --------------------------------------------------
    // Detect controller reboot
    // --------------------------------------------------

    if (lastHeartbeat > 0)
    {
        LOG_WARN("Controller reboot detected — restoring session");

        capState = CapState::UNKNOWN;

        sendCommand("CMD:STATUS\n");
    }

    lastHeartbeat = time(nullptr);
}

void DustCapIno::parseStatus(const char *buffer)
{
    lastHeartbeat = time(nullptr);
    lastStatusPacket = time(nullptr);
    movingCounter = 0;
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
    movingCounter = 0;
    double percent = 100 - (angle / 2.7);

    // snap to endpoints
    if (percent > 98)
        percent = 100;

    if (percent < 2)
        percent = 0;

    if (fabs(lastAngle - angle) > 1.0)
    {
        CoverAngleNP[0].setValue(percent);
        CoverAngleNP.apply();
        lastAngle = angle;
    }

    // state ALWAYS updated
    if (capState == CapState::MOVING)
        CoverAngleNP.setState(IPS_BUSY);
    else
        CoverAngleNP.setState(IPS_OK);

    CoverAngleNP.apply();

    CapState previousState = capState;

    if (strcmp(state, "IDLE_OPEN") == 0)
    {
        capState = CapState::OPEN;

        if (LightSP[0].getState() == ISS_ON)
        {
            LOG_WARN("Cap opened while light on — forcing light off");
            EnableLightBox(false);
        }
    }
    else if (strcmp(state, "IDLE_CLOSED") == 0)
    {
        capState = CapState::CLOSED;
    }
    else if (strcmp(state, "MOVING") == 0)
    {
        capState = CapState::MOVING;

        CoverAngleNP.setState(IPS_BUSY);
        CoverAngleNP.apply();
    }
    else
    {
        capState = CapState::ALERT;
    }
    // reset movement watchdog when valid STATUS arrives
    movingCounter = 0;

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
            ParkCapSP.setState(IPS_BUSY);
            break;

        case CapState::ALERT:
            ParkCapSP.setState(IPS_ALERT);
            break;

        default:
            break;
    }

    ParkCapSP.apply();

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
        sendCommand("CMD:STATUS\n");
}

void DustCapIno::parseDHT(const char *buffer)
{
    lastHeartbeat = time(nullptr);
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

void DustCapIno::parseDiagnostics(char *buffer)
{
    lastHeartbeat = time(nullptr);
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

    if (vcc < 3000)
        return;

    double voltage = vcc / 1000.0;
    int movingFlag = moving ? 1 : 0;

    bool changed = false;
    IPState state = IPS_OK;

    // Voltage
    if (fabs(lastVoltage - voltage) > 0.01)
    {
        HealthNP[0].setValue(voltage);
        lastVoltage = voltage;
        changed = true;
    }

    // RAM
    if (lastRAM != ram)
    {
        HealthNP[1].setValue(ram);
        lastRAM = ram;
        changed = true;
    }

    // Moving flag
    if (lastMoving != movingFlag)
    {
        HealthNP[3].setValue(movingFlag);
        lastMoving = movingFlag;
        changed = true;

        if (movingFlag)
            LOG_DEBUG("Servo movement active");
        else
            LOG_DEBUG("Servo movement stopped");
    }

    // --------------------------------------------------
    // Servo pulse diagnostics
    // --------------------------------------------------

    if (pulse < 0)
    {
        HealthNP[2].setValue(0);

        if (movingFlag)
        {
            if (lastPulse >= 0)
                LOG_WARN("Servo moving but no PWM detected");

            state = IPS_ALERT;
        }
        else
        {
            if (lastPulse >= 0)
                LOG_DEBUG("Servo idle (no PWM)");

            state = IPS_OK;
        }

        lastPulse = -1;
        changed = true;
    }
    else
    {
        if (lastPulse != pulse)
        {
            HealthNP[2].setValue(pulse);
            lastPulse = pulse;
            changed = true;
        }
    }

    // Voltage warning
    if (voltage < 4.4)
    {
        LOG_WARN("Controller voltage low");
        state = IPS_ALERT;
    }

    // Apply update only if something changed
    if (changed)
    {
        HealthNP.setState(state);
        HealthNP.apply();
    }
}

void DustCapIno::parseErrors(const char *buffer)
{
    if (strcmp(buffer, "SERVO_STALL") == 0)
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
    else if (strcmp(buffer, "FAILSAFE_CLOSE") == 0)
    {
        LOG_WARN("Fail-safe closing cap (host timeout)");
    }
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
            reconnectCounter = 0;

            LOG_WARN("Attempting serial reconnect...");

            if (autoDetectPort())
            {
                LOG_INFO("Serial reconnect successful");
                reconnectCounter = 0;
            }
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
        if (!readLine(buffer, nbytes_read))
            break;

        dispatchPacket(buffer);
    }

    time_t now = time(nullptr);

    if (lastHeartbeat > 0 && (now - lastHeartbeat) > 20)
    {
        LOG_WARN("Controller heartbeat lost — reconnecting");

        tty_disconnect(PortFD);
        PortFD = -1;

        reconnectCounter = reconnectInterval;
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
                sendCommand("CMD:CLOSE\n");
            else if (lastMove == MOVE_OPEN)
                sendCommand("CMD:OPEN\n");

            sendCommand("CMD:STATUS\n");

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
    // STATUS watchdog
    // --------------------------------------------------

    time_t nowWatch = time(nullptr);

    if (lastStatusPacket > 0 && (nowWatch - lastStatusPacket) > 15)
    {
        LOG_WARN("STATUS timeout detected");

        tty_disconnect(PortFD);
        PortFD = -1;

        reconnectCounter = reconnectInterval;
    }

    // --------------------------------------------------
    // Smart polling
    // --------------------------------------------------

    int interval = 1000;

    if (capState == CapState::MOVING)
        interval = 250;
    else if (capState == CapState::ALERT)
        interval = 2000;
    else
        interval = 1500;

    now = time(nullptr);

    // STATUS polling
    int statusInterval = (capState == CapState::MOVING) ? 1 : 3;

    if (now - lastStatusRequest >= statusInterval)
    {
        sendCommand("CMD:STATUS\n");
        lastStatusRequest = now;
    }

    // DHT polling
    static time_t lastDHT = 0;

    if (now - lastDHT > 10)
    {
        sendCommand("CMD:READ_DHT\n");
        lastDHT = now;
    }

    SetTimer(interval);
}