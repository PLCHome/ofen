#include "DataCare.h"
#include <unordered_map>

#include "../mySetup.h"
#include "../MessageDispatcher.h"

#include "io/beebDOut.h"
#include "io/modbDOut.h"
#include "io/modbTemp.h"
#include "io/pcf8574io.h"
#include "io/ws2812out.h"
#include "io/gpioDio.h"
#include "io/ds18b20Temp.h"

DataCare datacare = DataCare();

DataCare::DataCare()
{
    this->modbus = new Modbus();
    this->i2c = new I2C();
    this->beeb = new Beeb();
    this->gpio = new Gpio();
    this->ds18b20 = new DS18B20();
    this->ws2812 = new WS2812();
}

void TestDATAloop(void *pvParameters)
{
    datacare.DATAloop(pvParameters);
}

void DataCare::init()
{
    this->modbus->init();
    this->i2c->init();
    this->beeb->init();
    this->gpio->init();
    this->ds18b20->init();
    this->ws2812->init();

    createIO();

    initTempVals();
    initDiVals();
    initDoVals();
    initLedVals();

    Serial.print("DataCare: Tmp: ");
    Serial.print(this->getLenTemeratures());
    Serial.print(" Di: ");
    Serial.print(this->getLenInputs());
    Serial.print(" Do: ");
    Serial.print(this->getLenOutputs());
    Serial.println();

    xTaskCreate(
        TestDATAloop,
        "DATAloop",         // Name of the task (e.g. for debugging)
        2248,               // Stack size (bytes)
        NULL,               // Parameter to pass
        1,                  // Task priority
        &this->taskDataLoop // Task handle
    );
}

void DataCare::DATAloop(void *pvParameters)
{
    while (true)
    {
        uint32_t change = 0;
        if (this->CurrentLoop++ > loopsToRead)
        {
            CurrentLoop = 0;

            if (this->processTempValues())
            {
                change |= CHANGE_TEMP;
            }
            if (this->processDiValues())
            {
                change |= CHANGE_DI;
            }
        }

        if (this->processDoValues())
        {
            change |= CHANGE_DO;
        }
        if (this->processLedValues())
        {
            change |= CHANGE_LED;
        }
        if (change != 0)
            messagedispatcher.notify(change);

        uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, this->loopTime / portTICK_PERIOD_MS);
    }
}

void DataCare::notifyLoop()
{
    xTaskNotifyGive(this->taskDataLoop);
}

void DataCare::createIO()
{
    mysetup->resetSection();
    mysetup->setNextSection("io");
    Serial.print("Is Array? ");
    Serial.println(mysetup->isArrySection());
    if (mysetup->isArrySection())
    {
        uint16_t loop = 0;
        while (mysetup->setArrayElement(loop))
        {
            loop++;
            std::string card = mysetup->getArrayElementValue<std::string>("card", "");
            Serial.println(card.c_str());
            std::unordered_map<std::string, std::function<Datatool *()>> actions = {
                {"pcf8574", []()
                 { return new pcf8574io(); }},
                {"temperatures", []()
                 { return new modbTemp(); }},
                {"outputs", []()
                 { return new modbDOut(); }},
                {"beeb", []()
                 { return new beebDOut(); }},
                {"ws2812led", []()
                 { return new ws2812out(); }},
                {"gpio", []()
                 { return new gpioDio(); }},
                {"ds18b20s", []()
                 { return new ds18b20Temp(); }}};

            if (actions.find(card) != actions.end())
            {
                Datatool *newIO = actions[card]();
                newIO->init(this);
                datatools.push_back(newIO);
            }
            else
            {
                Serial.print(card.c_str());
                Serial.println(" not found!!");
            }
        }
    }
}

void DataCare::initTempVals()
{
    this->lenTemeratures = 0;
    for (const auto &datatool : this->datatools)
    {
        uint16_t a = datatool->getTempVals();
        if (a > 0)
        {
            datatool->setTempValsStart(this->lenTemeratures);
            this->lenTemeratures += a;
        }
    }
    if (this->lenTemeratures > 0)
    {
        this->temeratures = (int16_t *)calloc(this->lenTemeratures, sizeof(int16_t));
        this->lastTemeratures = (int16_t *)calloc(this->lenTemeratures, sizeof(int16_t));
    }
}

void DataCare::initDiVals()
{
    this->lenInputs = 0;
    for (const auto &datatool : this->datatools)
    {
        uint16_t a = datatool->getDIVals();
        if (a > 0)
        {
            datatool->setDIValsStart(this->lenInputs);
            this->lenInputs += a;
        }
    }
    if (this->lenInputs > 0)
    {
        this->inputs = (TA_INPUT *)calloc(this->lenInputs, sizeof(TA_INPUT));
    }
}

void DataCare::initDoVals()
{
    this->lenOutputs = 0;
    for (const auto &datatool : this->datatools)
    {
        uint16_t a = datatool->getDOVals();
        if (a > 0)
        {
            datatool->setDOValsStart(this->lenOutputs);
            this->lenOutputs += a;
        }
    }
    if (this->lenOutputs > 0)
    {
        this->outputs = (bool *)calloc(this->lenOutputs, sizeof(bool));
        this->lastOutputs = (bool *)calloc(this->lenOutputs, sizeof(bool));
    }
}

void DataCare::initLedVals()
{
    this->lenLeds = 0;
    for (const auto &datatool : this->datatools)
    {
        uint16_t a = datatool->getLedVals();
        if (a > 0)
        {
            datatool->setLedValsStart(this->lenLeds);
            this->lenLeds += a;
        }
    }
    if (this->lenLeds > 0)
    {
        this->leds = (uint32_t *)calloc(this->lenLeds, sizeof(uint32_t));
    }
}

bool DataCare::processTempValues()
{
    bool result = false;
    if (this->getDs18b20())
      this->getDs18b20()->requestTemperatures();
    for (const auto &datatool : this->datatools)
    {
        result |= datatool->processTempValues();
    }
    if (this->getDs18b20())
      this->getDs18b20()->temperaturesFinished();
    return result;
}

bool DataCare::processDoValues()
{
    bool result = false;
    for (const auto &datatool : this->datatools)
    {
        result |= datatool->processDoValues();
    }
    return result;
}
bool DataCare::processDiValues()
{
    bool result = false;
    for (const auto &datatool : this->datatools)
    {
        result |= datatool->processDiValues();
    }
    return result;
}

bool DataCare::processLedValues()
{
    bool result = false;
    for (const auto &datatool : this->datatools)
    {
        result |= datatool->processLedValues();
    }
    return result;
}

template <typename T>
String DataCare::jsonArray(String name, T buf[], int count) const
{
    JsonDocument doc;
    JsonArray data = (!name.isEmpty()) ? doc[name].to<JsonArray>() : doc.to<JsonArray>();

    for (int i = 0; i < count; i++)
    {
        data.add(buf[i]);
    }
    String out;
    serializeJson(doc, out);
    return out;
}

String DataCare::jsonTemeratures(bool obj) const
{
    return this->jsonArray(obj ? "tempholdingreg" : "", this->temeratures, this->lenTemeratures);
}

String DataCare::jsonDI(bool obj) const
{
    return this->jsonArray(obj ? "inputintern" : "", this->inputs, this->lenInputs);
}

String DataCare::jsonDO(bool obj) const
{
    return this->jsonArray(obj ? "relays" : "", this->outputs, this->lenOutputs);
}

String DataCare::jsonCounts(bool obj) const
{
    JsonDocument doc;
    JsonObject data = obj ? doc["data"].to<JsonObject>() : doc.to<JsonObject>();
    data["tmp"] = this->getLenTemeratures();
    data["in"] = this->getLenInputs();
    data["out"] = this->getLenOutputs();
    data["led"] = this->getLenLeds();
    String out;
    serializeJson(doc, out);
    return out;
}

Modbus *DataCare::getModbus() const
{
    return this->modbus;
}

I2C *DataCare::getI2C() const
{
    return this->i2c;
}

Beeb *DataCare::getBeeb() const
{
    return this->beeb;
}

Gpio *DataCare::getGpio() const
{
    return this->gpio;
}

DS18B20 *DataCare::getDs18b20() const
{
    return this->ds18b20;
}

int16_t *DataCare::getTemeratures(int16_t pos) const
{
    return &this->temeratures[pos];
}

int16_t *DataCare::getLastTemeratures(int16_t pos) const
{
    return &this->lastTemeratures[pos];
}

int16_t DataCare::getLenTemeratures() const
{
    return this->lenTemeratures;
}

TA_INPUT *DataCare::getInputs(int16_t pos) const
{
    return &this->inputs[pos];
}

int16_t DataCare::getLenInputs() const
{
    return this->lenInputs;
}

bool *DataCare::getOutputs(int16_t pos) const
{
    return &this->outputs[pos];
}

bool *DataCare::getLastOutputs(int16_t pos) const
{
    return &this->lastOutputs[pos];
}

int16_t DataCare::getLenOutputs() const
{
    return this->lenOutputs;
}

uint32_t *DataCare::getLeds(int16_t pos) const
{
    return &this->leds[pos];
}

int16_t DataCare::getLenLeds() const
{
    return this->lenLeds;
}