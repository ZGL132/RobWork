#include <gtest/gtest.h>

#include <rw/loaders/WorkCellLoader.hpp>

#include <fstream>
#include <string>

TEST (WorkCellLoaderFactory, reportsDetailedLoadError)
{
    std::string error;
    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load (
            "this-file-does-not-exist.wc.xml", &error);

    EXPECT_TRUE (workcell.isNull ());
    EXPECT_FALSE (error.empty ());
    EXPECT_NE (error.find ("this-file-does-not-exist.wc.xml"), std::string::npos);
}

TEST (WorkCellLoaderFactory, acceptsLegacyExplicitWorldFrame)
{
    const std::string filename = testing::TempDir () + "/legacy-explicit-world.wc.xml";
    std::ofstream file (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
    ASSERT_TRUE (file.good ());
    file << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<WorkCell name=\"Legacy\">\n"
            " <Frame name=\"WORLD\" refframe=\"WORLD\" type=\"Fixed\">\n"
            "  <RPY>0 0 0</RPY>\n"
            "  <Pos>0 0 0</Pos>\n"
            " </Frame>\n"
            " <Frame name=\"Base\" refframe=\"WORLD\" type=\"Fixed\">\n"
            "  <RPY>0 0 0</RPY>\n"
            "  <Pos>0 0 0</Pos>\n"
            " </Frame>\n"
            "</WorkCell>\n";
    file.close ();

    std::string error;
    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load (filename, &error);

    EXPECT_FALSE (workcell.isNull ()) << error;
    if (!workcell.isNull ()) {
        EXPECT_NE (workcell->findFrame<rw::kinematics::Frame> ("Base"), nullptr);
    }
}
