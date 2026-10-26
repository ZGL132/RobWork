#include <boost/json.hpp>
#include <cstdio>
int main(){
  std::printf("A\n");
  boost::json::parse_options opts;
  opts.max_depth = 32;
  std::printf("B\n");
  boost::json::value v = boost::json::parse("{\"a\":1,\"a\":2}", boost::json::storage_ptr(), opts);
  std::printf("C size=%zu\n", v.get_object().size());
  for (auto& kv : v.get_object())
    std::printf("D key=%s kind=%d\n", kv.key_c_str(), static_cast<int>(kv.value().kind()));
  return 0;
}
