#include <gtest/gtest.h>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/core/Compare.hpp>
#include <array>
#include <random>
#include <thread>
#include <unordered_map>
#include <iostream>
#include <limits>
#include <type_traits>
using namespace sdurws::ird::core;
// 隔离评审探针，只链接本次构建的产品库；不修改被测代码。
class CountSink final : public IDomainEventSink {
 public: int calls=0; void onEvent(const DomainEvent&) override { ++calls; }
};
TEST(ReviewEvents, HandleDestructionUnsubscribes_KnownF967) {
 ReferenceEventBus bus; CountSink sink;
 { auto handle=bus.subscribe(sink); ASSERT_EQ(bus.subscriberCount(),1u); }
 EXPECT_EQ(bus.subscriberCount(),0u);
 DomainEvent e; bus.publish(e); EXPECT_EQ(sink.calls,0);
}
TEST(ReviewEvents, KindAndVariantMismatchUsesCoreError) {
 auto e=DomainEvent::make(RevisionCommittedPayload{});
 e.kind=DomainEventKind::ResultArchived;
 EXPECT_THROW(e.asResultArchived(),CoreError);
 e=DomainEvent::make(DependencyInvalidatedPayload{});
 e.kind=DomainEventKind::RevisionCommitted;
 EXPECT_THROW(e.asRevisionCommitted(),CoreError);
 e=DomainEvent::make(ResultArchivedPayload{});
 e.kind=DomainEventKind::TaskStatusChanged;
 EXPECT_THROW(e.asTaskStatusChanged(),CoreError);
 e.kind=DomainEventKind::DependencyInvalidated;
 EXPECT_THROW(e.asDependencyInvalidated(),CoreError);
}
TEST(ReviewIdentity, StrongTypesAndAttemptBounds) {
 static_assert(!std::is_convertible_v<ObjectId,RevisionId>);
 static_assert(!std::is_convertible_v<ContentVersion,ContentIdentity>);
 const auto max=AttemptId::tryFromCanonical("att-18446744073709551615");
 ASSERT_TRUE(max); EXPECT_EQ(max->value,UINT64_MAX);
 for (auto s : {"att-18446744073709551616","att-01","att-+1","att--1","att-1 ","att-0"})
   EXPECT_FALSE(AttemptId::tryFromCanonical(s));
 auto old=ObjectId::fromCanonical("obj-0123456789abcdef0123456789abcdef");
 const auto saved=old;
 auto failed=ObjectId::tryFromCanonical("obj-0123456789abcdef0123456789abcdeg");
 EXPECT_FALSE(failed); EXPECT_EQ(old,saved);
}
TEST(ReviewIdentity, FirstTwoValuesInFreshThreadsAreDistinct) {
 std::cout << "random_device result bits=" << std::numeric_limits<std::random_device::result_type>::digits << std::endl;
 constexpr std::size_t n=131072, batch=32;
 std::vector<std::array<ObjectId,2>> values(n);
 for (std::size_t base=0;base<n;base+=batch) {
   std::vector<std::thread> threads;
   for(std::size_t i=base;i<base+batch;++i)
     threads.emplace_back([&,i]{values[i][0]=ObjectId::generate();values[i][1]=ObjectId::generate();});
   for(auto& t:threads) t.join();
 }
 std::unordered_map<ObjectId,std::size_t> first;
 std::size_t duplicates=0;
 for(std::size_t i=0;i<n;++i) {
   auto result=first.emplace(values[i][0],i);
   if(!result.second) {
     auto j=result.first->second; ++duplicates;
     std::cout << "duplicate threads=" << j << "," << i << " first=" << values[i][0].toCanonical()
       << " second-original=" << values[j][1].toCanonical() << " second-repeat=" << values[i][1].toCanonical() << std::endl;
     EXPECT_NE(values[j][1],values[i][1]) << "second full identity also duplicates";
   }
 }
 std::cout << "fresh_threads=" << n << " duplicate_full_ids=" << duplicates << std::endl;
 EXPECT_EQ(duplicates,0u);
}
TEST(ReviewBoundaries, UnitsAndStatesCharacterization) {
 UnitToken invalid; EXPECT_FALSE(invalid.isValid());
 const auto m=*UnitToken::find("m");
 EXPECT_FALSE(tryConvert(1,invalid,m)); EXPECT_THROW(convert(1,invalid,m),CoreError);
 // 已注册前置外不调用kind/symbol/Quantity入口，不执行未定义行为。
 const auto mm=*UnitToken::find("mm");
 const auto tiny=std::numeric_limits<double>::denorm_min();
 ASSERT_TRUE(tryConvert(tiny,mm,mm)); EXPECT_EQ(*tryConvert(tiny,mm,mm),0.0);
 const auto si=Length::fromSi(tiny); EXPECT_EQ(si.siValue(),tiny);
 EXPECT_STREQ(toToken(static_cast<TaskState>(255)),"unknown");
 EXPECT_FALSE(taskStateFromToken("unknown"));
 const auto missing=SourcedValue<double>::notProvided(); EXPECT_FALSE(missing.tryValue());
 EXPECT_THROW(missing.value(),CoreError);
}
