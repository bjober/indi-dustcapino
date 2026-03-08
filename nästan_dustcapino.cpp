#include <memory>
#include <defaultdevice.h>
#include <indilogger.h>

class DustCapIno : public INDI::DefaultDevice
{
public:
    DustCapIno()
    {
        // Viktigt i classic C-entry model
        setDeviceName("DustCapIno");
    }

    virtual ~DustCapIno() = default;

    const char *getDefaultName() override
    {
        return "DustCapIno";
    }

    bool initProperties() override
    {
        INDI::DefaultDevice::initProperties();

        // AUX driver
        setDriverInterface(AUX_INTERFACE);

        // Registrerar CONNECTION + DEBUG + CONFIG + POLLING
        addAuxControls();

        return true;
    }

    bool updateProperties() override
    {
        INDI::DefaultDevice::updateProperties();

        if (isConnected())
        {
            DEBUG(INDI::Logger::DBG_ERROR, ">>> CONNECTED <<<");

            // Här öppnar du seriell port senare
            // openPort();
        }
        else
        {
            DEBUG(INDI::Logger::DBG_ERROR, ">>> DISCONNECTED <<<");

            // Här stänger du port senare
            // closePort();
        }

        return true;
    }
};

/* ------------------------------------------------------------------------- */
/* ---------------- Classic C entry points (måste finnas) ------------------ */
/* ------------------------------------------------------------------------- */

std::unique_ptr<DustCapIno> dustcapino;

extern "C"
{

void ISGetProperties(const char *dev)
{
    if (!dustcapino)
        dustcapino.reset(new DustCapIno());

    dustcapino->ISGetProperties(dev);
}

void ISNewSwitch(const char *dev, const char *name,
                 ISState *states, char *names[], int n)
{
    if (dustcapino)
        dustcapino->ISNewSwitch(dev, name, states, names, n);
}

void ISNewText(const char *dev, const char *name,
               char *texts[], char *names[], int n)
{
    if (dustcapino)
        dustcapino->ISNewText(dev, name, texts, names, n);
}

void ISNewNumber(const char *dev, const char *name,
                 double values[], char *names[], int n)
{
    if (dustcapino)
        dustcapino->ISNewNumber(dev, name, values, names, n);
}

void ISSnoopDevice(XMLEle *root)
{
    if (dustcapino)
        dustcapino->ISSnoopDevice(root);
}

}