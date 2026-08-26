#include "../../visualization/WebServer.h"

#include <gtest/gtest.h>

namespace pinnacle::visualization {

class RestHandler : public ::testing::Test {
protected:
  std::shared_ptr<PerformanceCollector> collector =
      std::make_shared<PerformanceCollector>();
  RestAPIServer server{collector};

  http::response<http::string_body> get(const std::string& target) {
    http::request<http::string_body> request{http::verb::get, target, 11};
    return server.handleRequest(std::move(request));
  }
};

class PerformanceHistory : public ::testing::Test {
protected:
  PerformanceCollector collector;
};

TEST_F(PerformanceHistory, FiltersSnapshotsInclusivelyByTimeRange) {
  PerformanceData first;
  first.timestamp = 100;
  first.pnl = 1.0;
  PerformanceData second;
  second.timestamp = 200;
  second.pnl = 2.0;
  PerformanceData third;
  third.timestamp = 300;
  third.pnl = 3.0;

  collector.recordPerformance("strategy", first);
  collector.recordPerformance("strategy", second);
  collector.recordPerformance("strategy", third);

  auto history = collector.getPerformanceHistory("strategy", 200, 300);

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0].timestamp, 200);
  EXPECT_EQ(history[1].timestamp, 300);
}

TEST_F(PerformanceHistory, AppliesMaximumHistorySize) {
  collector.setMaxHistorySize(2);

  for (uint64_t timestamp = 1; timestamp <= 3; ++timestamp) {
    PerformanceData data;
    data.timestamp = timestamp;
    collector.recordPerformance("strategy", data);
  }

  auto history = collector.getPerformanceHistory("strategy", 0, 3);

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0].timestamp, 2);
  EXPECT_EQ(history[1].timestamp, 3);
}

TEST_F(PerformanceHistory, AppliesQueryResultLimit) {
  for (uint64_t timestamp = 1; timestamp <= 3; ++timestamp) {
    PerformanceData data;
    data.timestamp = timestamp;
    collector.recordPerformance("strategy", data);
  }

  auto history = collector.getPerformanceHistory("strategy", 0, 3, 2);

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0].timestamp, 1);
  EXPECT_EQ(history[1].timestamp, 2);
}

TEST(ChartHistory, FiltersPointsByTimeRange) {
  PerformanceCollector collector;
  ChartDataPoint first;
  first.timestamp = 100;
  first.value = 1.0;
  ChartDataPoint second;
  second.timestamp = 200;
  second.value = 2.0;
  ChartDataPoint third;
  third.timestamp = 300;
  third.value = 3.0;

  collector.recordChartData("strategy", "pnl", first);
  collector.recordChartData("strategy", "pnl", second);
  collector.recordChartData("strategy", "pnl", third);

  auto history = collector.getChartDataInRange("strategy", "pnl", 200, 300);

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0].timestamp, 200);
  EXPECT_EQ(history[1].timestamp, 300);
}

TEST(QueryString, ParsesStandardQueryParameters) {
  auto params = parseQueryString("start=1234567890&end=9876543210&limit=100");

  EXPECT_EQ(params.at("start"), "1234567890");
  EXPECT_EQ(params.at("end"), "9876543210");
  EXPECT_EQ(params.at("limit"), "100");
}

TEST(QueryString, SupportsEncodedValuesAndOptionalQuestionMark) {
  auto params =
      parseQueryString("?name=Oore%20Fasawe&search=C%2B%2B&note=hello+world");

  EXPECT_EQ(params.at("name"), "Oore Fasawe");
  EXPECT_EQ(params.at("search"), "C++");
  EXPECT_EQ(params.at("note"), "hello world");
}

TEST(QueryString, ReturnsEmptyMapForEmptyQuery) {
  EXPECT_TRUE(parseQueryString("").empty());
}

TEST(QueryString, HandlesEmptyMissingAndRepeatedValues) {
  auto params = parseQueryString("limit=&debug&metric=pnl&metric=sharpe");

  EXPECT_EQ(params.at("limit"), "");
  EXPECT_EQ(params.at("debug"), "");
  EXPECT_EQ(params.at("metric"), "sharpe");
}

TEST(QueryString, PreservesEqualsCharactersInValues) {
  auto params = parseQueryString("token=abc=123");

  EXPECT_EQ(params.at("token"), "abc=123");
}

TEST(QueryString, SkipsMalformedParameters) {
  auto params =
      parseQueryString("valid=value&bad=%ZZ&truncated=%A&empty-key=value");

  EXPECT_EQ(params.at("valid"), "value");
  EXPECT_EQ(params.count("bad"), 0);
  EXPECT_EQ(params.count("truncated"), 0);
  EXPECT_EQ(params.count("empty-key"), 1);
}

TEST(ChartRouting, ExtractsStrategyAndMetricPath) {
  EXPECT_EQ(extractPath("/api/v1/strategies/strategy/charts/pnl?range=1h"),
            "/api/v1/strategies/strategy/charts/pnl");
}

TEST_F(RestHandler, ReturnsFilteredPerformanceHistoryAndPreservesLatestShape) {
  PerformanceData first;
  first.timestamp = 100;
  first.pnl = 1.0;
  PerformanceData second;
  second.timestamp = 200;
  second.pnl = 2.0;
  collector->recordPerformance("strategy", first);
  collector->recordPerformance("strategy", second);

  auto ranged =
      get("/api/v1/strategies/strategy/performance?start=100&end=200");
  auto latest = get("/api/v1/strategies/strategy/performance");

  ASSERT_EQ(ranged.result(), http::status::ok);
  EXPECT_TRUE(nlohmann::json::parse(ranged.body())["data"].is_array());
  EXPECT_TRUE(nlohmann::json::parse(latest.body())["data"].is_object());
}

TEST_F(RestHandler, RejectsInvalidAndReversedPerformanceRanges) {
  EXPECT_EQ(get("/api/v1/strategies/strategy/performance?start=abc").result(),
            http::status::bad_request);
  EXPECT_EQ(
      get("/api/v1/strategies/strategy/performance?start=200&end=100").result(),
      http::status::bad_request);
}

TEST_F(RestHandler, ReturnsEmptyArrayForPerformanceRangeWithNoMatches) {
  PerformanceData data;
  data.timestamp = 100;
  collector->recordPerformance("strategy", data);

  auto response =
      get("/api/v1/strategies/strategy/performance?start=200&end=300");
  auto body = nlohmann::json::parse(response.body());

  ASSERT_EQ(response.result(), http::status::ok);
  EXPECT_TRUE(body["data"].is_array());
  EXPECT_TRUE(body["data"].empty());
}

TEST_F(RestHandler, RejectsInvalidPerformanceLimit) {
  EXPECT_EQ(get("/api/v1/strategies/strategy/performance?limit=abc").result(),
            http::status::bad_request);
}

TEST_F(PerformanceHistory, UnregisterClearsHistory) {
  PerformanceData data;
  data.timestamp = 100;
  collector.recordPerformance("strategy", data);
  collector.unregisterStrategy("strategy");

  EXPECT_TRUE(collector.getPerformanceHistory("strategy", 0, 200).empty());
}

} // namespace pinnacle::visualization
