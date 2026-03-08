#pragma once

#include <defaultdevice.h>
#include <indilightboxinterface.h>
#include <indidustcapinterface.h>

class DustCapIno : public INDI::DefaultDevice,
                   public INDI::LightBoxInterface,
                   public INDI::DustCapInterface
{
public:
    DustCapIno();
    ~DustCapIno() override = default;

    const char *getDefaultName() override;

    bool initProperties() override;
    bool updateProperties() override;

    bool Connect() override;
    bool Disconnect() override;

    IPState ParkCap() override;
    IPState UnParkCap() override;

    bool EnableLightBox(bool enable) override;
    bool SetLightBoxBrightness(uint16_t value) override;

    bool ISNewSwitch(const char *dev,
                     const char *name,
                     ISState *states,
                     char *names[],
                     int n) override;

    bool ISNewNumber(const char *dev,
                     const char *name,
                     double *values,
                     char **names,
                     int n) override;

    void TimerHit() override;

private:

    // ==================================================
    // State machine
    // ==================================================

    enum class CapState
    {
        UNKNOWN,
        OPEN,
        CLOSED,
        MOVING,
        ALERT
    };

    enum LastMove
    {
        MOVE_NONE,
        MOVE_OPEN,
        MOVE_CLOSE
    };

    CapState capState = CapState::UNKNOWN;
    LastMove lastMove = MOVE_NONE;

    // ==================================================
    // Serial
    // ==================================================

    int PortFD {-1};
    bool autoDetectPort();

    // ==================================================
    // Watchdogs / recovery
    // ==================================================

    int movingCounter = 0;
    int statusTimeoutCounter = 0;

    int reconnectCounter = 0;
    int reconnectInterval = 5;

    std::string firmwareVersion;

    // ==================================================
    // Cached device state
    // ==================================================

    double lastAngle = 270.0;
    int lastBrightness = 0;
    bool lastSafe = true;
    double lastTemp = 0;
    double lastHum = 0;

    // ==================================================
    // INDI Properties
    // ==================================================

    INDI::PropertyText   SerialPortTP{1};
    INDI::PropertySwitch BaudRateSP{4};
    INDI::PropertySwitch SafetyOverrideSP{2};
    INDI::PropertyNumber CoverProgressNP{1};
    INDI::PropertyNumber CoverAngleNP{1};
    INDI::PropertyNumber EnvNP{2};
    INDI::PropertyNumber HealthNP{4};
    INDI::PropertyText HandshakeTP{1};
    INDI::PropertyText FirmwareInfoTP{3};
    INDI::PropertySwitch MaintenanceSP{5};
    INDI::PropertyText StatusTP{4};
};