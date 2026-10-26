#include <boost/json.hpp>
#include <boost/system/system_error.hpp>
#include <cstdio>
int main(){
  try {
    boost::json::value v = boost::json::parse("{ \"a\" : 1 }");
    std::printf("unexpected ok\n");
  } catch (const boost::system::system_error& e) {
    std::printf("caught syserr val=%d msg=%s\n", static_cast<int>(e.code().value()), e.code().message().c_str());
  } catch (const std::exception& e) {
    std::printf("caught std exc=%s\n", e.what());
  }
  return 0;
}
