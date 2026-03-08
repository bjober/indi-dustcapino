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
    int PortFD {-1};
    int movingCounter = 0;
    int movingCounter = 0;
    int statusTimeoutCounter = 0;
    bool capIsParked = false;
    bool capIsMoving = false;
    bool autoDetectPort();

    INDI::PropertyText   SerialPortTP{1};
    INDI::PropertySwitch BaudRateSP{4};

    INDI::PropertySwitch SafetyOverrideSP{2};
    INDI::PropertyNumber CoverAngleNP{1};
    INDI::PropertyNumber EnvNP{2};
};