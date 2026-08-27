/********************************************************************************
 * Copyright 2017 The Robotics Group, The Maersk Mc-Kinney Moller Institute,
 * Faculty of Engineering, University of Southern Denmark
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ********************************************************************************/

#include <gtest/gtest.h>
#include "../TestEnvironment.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/geometry/Model3D.hpp>
#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/loaders/dom/DOMProximitySetupSaver.hpp>
#include <rw/loaders/dom/DOMWorkCellSaver.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/ProximitySetup.hpp>
#include <rw/proximity/CollisionDetector.hpp>
#include <rw/proximity/BasicFilterStrategy.hpp>
#include <rwlibs/proximitystrategies/ProximityStrategyFactory.hpp>

#include <cmath>
#include <string>
#include <fstream>

using namespace rw::core;
using namespace rw::loaders;
using namespace rw::proximity;
using namespace std;

class DOMProximitySetupSaverTest : public ::testing::Test {
public:

    const string workcellFile = TestEnvironment::testfilesDir() + "/workcells/simple_wc/SimpleWorkcell.wc.xml";
    const string workcellFileUsingSerializedProx = TestEnvironment::testfilesDir() + "/workcells/simple_wc/SimpleWorkcellUsingSerializedProx.wc.xml";
    rw::models::WorkCell::Ptr wc;
    rw::models::WorkCell::Ptr wcUsingProx;
    rw::proximity::ProximitySetup proxSetup;
    rw::proximity::ProximitySetup serializedProxSetup;
    BasicFilterStrategy::Ptr broadphase;

    virtual void SetUp(){
        // load simple workcell for testing
        wc = WorkCellLoader::Factory::load(workcellFile);
        // obtain current proximity setup from workcell
        proxSetup = rw::proximity::ProximitySetup::get(*wc);
    }
};

TEST_F (DOMProximitySetupSaverTest, LoadWCSerializeProxAndReload)
{
    /*
     *  At this stage the test workcell and the associated proximity setup has been loaded. [see SetUp()]
     */

    // Get where to save the proximity setup
    const std::string serializationFilepath = TestEnvironment::testfilesDir() + "/workcells/simple_wc/SerializedProximitySetup.xml";

    // Save current proximity setup using the DOMProximitySetupSaver
    rw::loaders::DOMProximitySetupSaver::save(proxSetup, serializationFilepath);

    // Load the workcell that is using the serialized proximity setup
    wcUsingProx = WorkCellLoader::Factory::load(workcellFileUsingSerializedProx);
    serializedProxSetup = rw::proximity::ProximitySetup::get(*wcUsingProx);

    // Perform a collision detection on the default wc state
    CollisionDetector::Ptr cd = rw::core::ownedPtr(new CollisionDetector(
            wcUsingProx,
            rwlibs::proximitystrategies::ProximityStrategyFactory::makeDefaultCollisionStrategy()));
    CollisionDetector::QueryResult res;
    cd->inCollision(wcUsingProx->getDefaultState(), &res);

    // We expect 0 collisions if the proximity setup has been serialized and loaded properly
    EXPECT_EQ(res.collidingFrames.size(), 0u);

    // Compare number of rules in the original proximity setup with the serialized one.
    EXPECT_EQ(proxSetup.getProximitySetupRules().size(), serializedProxSetup.getProximitySetupRules().size());
}

TEST (DOMWorkCellSaverTest, CollisionSetupOnlyWorkCellDoesNotWriteToEmptyProximityPath)
{
    const std::string source = TestEnvironment::testfilesDir () + "/simple/workcell.wc.xml";
    const rw::models::WorkCell::Ptr workcell = WorkCellLoader::Factory::load (source);
    ASSERT_TRUE (workcell != nullptr);

    // XMLRWLoader represents a WorkCell without ProximitySetup using empty path properties.
    // Set the contract explicitly so this regression remains independent of path normalization.
    workcell->getPropertyMap ().set< std::string > ("ProximitySetupFilePath", std::string ());
    workcell->getPropertyMap ().set< std::string > ("ProximitySetupRelFilePath", std::string ());
    const std::string proximityPath = workcell->getPropertyMap ().get< std::string > (
        "ProximitySetupFilePath", std::string ());
    ASSERT_TRUE (proximityPath.empty ());

    const std::string target = ::testing::TempDir () + "CollisionSetupOnlyWorkCell.wc.xml";
    EXPECT_NO_THROW (rw::loaders::DOMWorkCellSaver::save (
        workcell, workcell->getDefaultState (), target));

    std::ifstream saved (target.c_str ());
    ASSERT_TRUE (saved.good ());
    const std::string xml ((std::istreambuf_iterator< char > (saved)),
                           std::istreambuf_iterator< char > ());
    EXPECT_EQ (std::string::npos, xml.find ("<ProximitySetup file=\"\""));

    workcell->getPropertyMap ().erase ("ProximitySetupFilePath");
    workcell->getPropertyMap ().erase ("ProximitySetupRelFilePath");
    const std::string missingPropertiesTarget =
        ::testing::TempDir () + "MissingProximityPropertiesWorkCell.wc.xml";
    EXPECT_NO_THROW (rw::loaders::DOMWorkCellSaver::save (
        workcell, workcell->getDefaultState (), missingPropertiesTarget));
}

TEST (DOMWorkCellSaverTest, PreservesDrawableRgbOnRoundTrip)
{
    const std::string sourcePath = ::testing::TempDir () + "ColoredDrawable.wc.xml";
    {
        std::ofstream source (sourcePath.c_str ());
        ASSERT_TRUE (source.good ());
        source << "<WorkCell name=\"ColoredDrawable\">\n"
               << "  <Frame name=\"ColoredPart\" refframe=\"WORLD\">\n"
               << "    <Drawable name=\"colored_box\">\n"
               << "      <RGB>0.2 0.4 0.8</RGB>\n"
               << "      <Box x=\"0.1\" y=\"0.2\" z=\"0.3\" />\n"
               << "    </Drawable>\n"
               << "  </Frame>\n"
               << "</WorkCell>\n";
    }

    const rw::models::WorkCell::Ptr original =
        WorkCellLoader::Factory::load (sourcePath);
    ASSERT_TRUE (original != nullptr);

    const std::string targetPath = ::testing::TempDir () + "ColoredDrawableSaved.wc.xml";
    EXPECT_NO_THROW (rw::loaders::DOMWorkCellSaver::save (
        original, original->getDefaultState (), targetPath));

    std::ifstream saved (targetPath.c_str ());
    ASSERT_TRUE (saved.good ());
    const std::string xml ((std::istreambuf_iterator< char > (saved)),
                           std::istreambuf_iterator< char > ());
    EXPECT_NE (std::string::npos, xml.find ("<RGB>"));
    EXPECT_NE (std::string::npos, xml.find ("0.2"));
    EXPECT_NE (std::string::npos, xml.find ("0.4"));
    EXPECT_NE (std::string::npos, xml.find ("0.8"));

    const rw::models::WorkCell::Ptr reloaded =
        WorkCellLoader::Factory::load (targetPath);
    ASSERT_TRUE (reloaded != nullptr);
    const rw::models::Object::Ptr object = reloaded->findObject ("ColoredPart");
    ASSERT_TRUE (object != nullptr);
    ASSERT_EQ (1u, object->getModels ().size ());
    ASSERT_FALSE (object->getModels ().front ()->getMaterials ().empty ());

    const rw::geometry::Model3D::Material& material =
        object->getModels ().front ()->getMaterials ().front ();
    EXPECT_TRUE (std::abs (material.rgb[0] - 0.2f) < 1e-5f);
    EXPECT_TRUE (std::abs (material.rgb[1] - 0.4f) < 1e-5f);
    EXPECT_TRUE (std::abs (material.rgb[2] - 0.8f) < 1e-5f);
}

