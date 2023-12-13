#include "SystemCouplingParticipant/SystemCoupling.hpp"

#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using Vector = std::array<double, 3>;
using NodeCoords = std::vector<Vector>;
using Ids = std::vector<std::size_t>;

struct PointCloud {
    Ids ids;
    NodeCoords coords;

    std::size_t size() const noexcept
    {
        return ids.size();
    }
};

using PointClouds = std::map<sysc::RegionName, PointCloud>;
using VariableData = std::map<sysc::VariableName, std::vector<double>>;
using SolutionData = std::map<sysc::RegionName, VariableData>;

PointClouds pointClouds;
SolutionData solutionData;

using RegionIndex = std::size_t;
using LocalId = std::size_t;
std::vector<std::pair<RegionIndex, LocalId>> globalIdToRegionIndex;
std::vector<sysc::Region> regions;

bool debug{ false };
int timeStep = 0;

// convert C# string to C++ std::string
// largely copied from MS website
std::string getStdString(System::String^ s) {
    using namespace System::Runtime::InteropServices;
    const char* chars = (const char*)(Marshal::StringToHGlobalAnsi(s)).ToPointer();
    std::string stdString(chars);
    Marshal::FreeHGlobal(System::IntPtr((void*)chars));
    return stdString;
}

sysc::PointCloud getPointCloud(const sysc::RegionName& regionName)
{
    if (debug) {
        std::cout << "getPointCloud called for region " << regionName << '\n';
    }
    const auto& thisPc = pointClouds.at(regionName);
    sysc::PointCloud pc(
        sysc::OutputIntegerData(thisPc.ids),
        sysc::OutputVectorData(thisPc.coords.front().data(), thisPc.coords.size()));

    if (debug) {
        auto validity = pc.checkValidity();
        if (!validity.isValid) {
            throw std::runtime_error("Point cloud validity check failed: " + validity.message);
        }
    }
    return pc;
}

sysc::OutputScalarData getOutputScalar(const sysc::RegionName& regionName, const sysc::VariableName& variableName)
{
    return sysc::OutputScalarData(solutionData.at(regionName).at(variableName));
}

std::string getRestartPoint()
{
    return std::to_string(timeStep);
}

void printData()
{
    for (const auto& [regionName, pointCloud] : pointClouds) {
        std::cout << "Region name = " << regionName << '\n';
        std::cout << "  Size = " << pointCloud.size() << std::endl;
        for (std::size_t i = 0; i < pointCloud.size(); ++i) {
            std::cout << pointCloud.ids[i] << ", " << pointCloud.coords[i][0] << ", " << pointCloud.coords[i][1] << ", " << pointCloud.coords[i][2] << '\n';
        }
    }
}

int getDim(System::String^ sindaDesignator)
{
    if (sindaDesignator->Contains("POS_X")) {
        return 0;
    }
    else if (sindaDesignator->Contains("POS_Y")) {
        return 1;
    }
    else if (sindaDesignator->Contains("POS_Z")) {
        return 2;
    }
    else {
        throw std::runtime_error("ERROR: getDim unexpected SINDA designator");
    }
}

bool isEmptyRegion(OpenTDv232::Results::Dataset::SaveFile^ tdFile, System::String^ submodel)
{
    OpenTDv232::Results::Dataset::ItemIdentifierCollection allbarNodeNames(OpenTDv232::Results::Dataset::DataTypes::NODE, submodel, tdFile);
    OpenTDv232::Results::Dataset::DataItemIdentifierCollection allBar_T_Names(% allbarNodeNames, OpenTDv232::Results::Dataset::StandardDataSubtypes::T);
    auto temperatureData = tdFile->GetData(% allBar_T_Names);

    if (temperatureData->Count > 0) {
        return false;
    }
    return true;
}

void fillTimeStepData(OpenTDv232::Results::Dataset::SaveFile^ tdFile, int timeStep)
{
    // record name is more like a time step
    // udfa description is more like a field
    // id is more like a local id, whereas internalIndex is more like a global id
    // so when we loop below, we are filling a single global array of data,
    // and it's not sorted by regions

    auto recordNumbers = tdFile->GetRecordNumbers();
    auto recordNames = tdFile->GetRecordNames();
    auto currRecordNum = recordNumbers[timeStep];
    auto udfaDescriptions = tdFile->GetUdfasAtRecord(currRecordNum);
    std::cout << "Number of udfaDescriptions = " << udfaDescriptions->Count << std::endl;
    for each (auto udfaDescription in udfaDescriptions)
    {
        System::Console::WriteLine("SINDA DESIGNATOR = " + udfaDescription->SindaDesignator);
        if (!udfaDescription->SindaDesignator->Contains("POS_")) {
            System::Console::WriteLine("SKIPPING");
            continue;
        }

        // we've found a position UDFA, based on naming convention in this model        
        auto udfaData = tdFile->GetDataAtRecord(udfaDescription, currRecordNum);
        for (int globalId = 0; globalId < udfaData->Count; ++globalId) {
            auto coordValue = udfaData[globalId];
            const auto& [regionIndex, localId] = globalIdToRegionIndex.at(globalId);
            auto dim = getDim(udfaDescription->SindaDesignator);
            pointClouds.at(regions.at(regionIndex).getName()).coords.at(localId)[dim] = coordValue;
        }
    }

    for each (auto submodel in tdFile->GetThermalSubmodels())
    {
        auto regionName = getStdString(submodel);

        OpenTDv232::Results::Dataset::ItemIdentifierCollection allbarNodeNames(OpenTDv232::Results::Dataset::DataTypes::NODE, submodel, tdFile);
        OpenTDv232::Results::Dataset::DataItemIdentifierCollection allBar_T_Names(% allbarNodeNames, OpenTDv232::Results::Dataset::StandardDataSubtypes::T);
        auto temperatureData = tdFile->GetData(% allBar_T_Names);

        if (temperatureData->Count == 0) {
            continue;
        }

        for (int index = 0; index < temperatureData->Count; ++index) {
            auto dataPoint = temperatureData[index];
            auto valueAtTimeStep = dataPoint[timeStep];
            solutionData.at(regionName).at("Temperature").at(index) = valueAtTimeStep;
        }

    }
}

void readData(OpenTDv232::Results::Dataset::SaveFile^ tdFile)
{
    // submodel is equivalent to a System Coupling region

    for each (auto submodel in tdFile->GetThermalSubmodels())
    {
        if (isEmptyRegion(tdFile, submodel)) {
            continue;
        }

        auto regionName = getStdString(submodel);
        std::cout << "  New region: " << regionName << std::endl;

        auto regionIndex = regions.size();
        regions.emplace_back(sysc::Region(regionName, sysc::Topology::Volume, sysc::RegionDiscretizationType::PointCloudRegion));

        pointClouds.emplace(std::make_pair(regionName, PointCloud()));
        auto& pointCloud = pointClouds.at(regionName);

        solutionData.emplace(std::make_pair(regionName, VariableData()));
        solutionData.at(regionName).emplace(std::make_pair("Temperature", std::vector<double>()));
        auto& currTemperature = solutionData.at(regionName).at("Temperature");

        for each (auto id in tdFile->GetNodeIds(submodel))
        {
            long globalId = tdFile->GetInternalIndex(OpenTDv232::Results::Dataset::DataTypes::NODE, submodel, id);
            long localId = id - 1;
            if (globalIdToRegionIndex.size() < globalId + 1) {
                globalIdToRegionIndex.resize(globalId + 1);
            }
            globalIdToRegionIndex[globalId] = std::make_pair(regionIndex, localId);

            pointCloud.ids.emplace_back(globalId);
            pointCloud.coords.emplace_back(Vector()); // coordinates to be filled later
            currTemperature.emplace_back(300.0);
        }
    }

    if (debug) {
        auto times = tdFile->GetTimes()->GetValues();
        for (int i = 0; i < times->Count; i++)
        {
            System::Console::WriteLine("Time " + i + ": " + times[i]);
        }
    }
}

int main(array<System::String^>^ args)
{
    std::string host("#");
    std::string name("TD");
    unsigned short port{ 0 };
    const std::string buildInfo("TD Results Reader v0.1");
    bool scsetup{ false };
    bool writeScp{ false };

    System::String^ fileName;

    for (int i = 0; i < args->Length; ++i) {
        if (args[i] == "--schost") {
            host = getStdString(args[i + 1]);
        }
        if (args[i] == "--scname") {
            name = getStdString(args[i + 1]);
        }
        if (args[i] == "--scport") {
            std::string portStr;
            port = std::stoi(getStdString(args[i + 1]));
        }
        if (args[i] == "--scsetup") {
            scsetup = true;
        }
        if (args[i] == "--writescp") {
            writeScp = true;
        }
        if (args[i] == "--input") {
            fileName = args[i + 1];
        }
        if (args[i] == "--debug") {
            debug = true;
        }
    }

    if (fileName == "") {
        std::cout << "File name must be specified via --input argument";
        return EXIT_FAILURE;
    }

    std::cout << "schost  = " << host << '\n';
    std::cout << "scport  = " << port << '\n';
    std::cout << "scname  = " << name << '\n';
    std::cout << "scsetup = " << scsetup << '\n';
    System::Console::WriteLine("filename = " + fileName);

    sysc::ParticipantInfo partInfo(host, port, name, buildInfo);
    partInfo.transcriptFilename = name + ".stdout";
    sysc::SystemCoupling sc(partInfo);
    std::cout << "Connected\n";

    OpenTDv232::Results::Dataset::SaveFile tdFile(fileName);
    readData(% tdFile);

    try {
        if (scsetup) {

            for (const auto& [regionName, pointCloud] : pointClouds) {
                sysc::Region region(regionName, sysc::Topology::Volume, sysc::RegionDiscretizationType::PointCloudRegion);
                sysc::Variable temperature("Temperature", sysc::TensorType::Scalar, false, sysc::Location::Node);
                region.addOutputVariable(temperature);
                sc.addRegion(region);
            }

            sysc::SetupInfo setupInfo;
            setupInfo.analysisType = sysc::AnalysisType::Transient;
            setupInfo.restartsSupported = true;
            sc.completeSetup(setupInfo);
            if (writeScp) {
                sc.writeSetupFile(sysc::SetupFileInfo("setup.scp"));
            }
        }
        else {

            sc.registerPointCloudAccess(&getPointCloud);
            sc.registerOutputScalarDataAccess(&getOutputScalar);
            sc.registerRestartPointCreation(&getRestartPoint);
            std::cout << "Registered callbacks\n";

            fillTimeStepData(% tdFile, timeStep);

            sc.initializeAnalysis();
            std::cout << "Initialized analysis\n";

            while (sc.doTimeStep()) {
                ++timeStep;
                std::cout << "  time step\n";
                fillTimeStepData(% tdFile, timeStep);
                while (sc.doIteration()) {
                    std::cout << "    iteration\n";
                    sc.updateInputs();
                    std::cout << "      updated inputs\n";
                    sc.updateOutputs(sysc::ConvergenceStatus::NotEvaluated);
                    std::cout << "      updated outputs\n";
                }
            }

        }
    }
    catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what();
        return EXIT_FAILURE;
    }

    sc.disconnect();
    std::cout << "disconnected - all ok\n";

    return EXIT_SUCCESS;
}

