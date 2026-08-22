#include "FourPluginAcceptanceFixture.hpp"

#include <QCoreApplication>

#include <iostream>

int main (int argc, char** argv)
{
    QCoreApplication application (argc, argv);
    const sdurws::fourpluginacceptance::FixtureResult result =
        sdurws::fourpluginacceptance::runFourPluginAcceptanceFixture ();
    std::cout << "S-B1-FIXTURE " << (result.passed ? "PASS" : "FAIL")
              << " error-code=" << result.errorCode.toStdString ()
              << " duration-ms=" << result.durationMs << std::endl;
    if (!result.passed)
        std::cerr << result.errorMessage.toStdString () << std::endl;
    return result.passed ? 0 : 1;
}
