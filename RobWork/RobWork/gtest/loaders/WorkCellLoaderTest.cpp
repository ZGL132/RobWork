#include <gtest/gtest.h>

#include <rw/loaders/WorkCellLoader.hpp>

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
