// Exercise the same parser, paired runner, JSON writer and exclusive saver used
// by the executable, without introducing another scoring/simulation engine.
#define ZILCH_RESEARCH_TESTING
#include "strategy_lab.cpp"

namespace {

void check(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Config selectionConfig()
{
    Config config;
    config.mode = "selection";
    config.atRisk = 0;
    config.roll = {6, 6, 6, 5, 2, 3};
    config.selectLeft = {6, 6, 6, 5};
    config.selectRight = {6, 6, 6};
    config.collectA = true;
    config.collectB = true;
    config.pairs = 64;
    return config;
}

void parserAndMetadata()
{
    const char* args[] = {"zilch_research", "--mode", "selection", "--roll", "6,6,6,5,2,3",
        "--select-left", "6,6,6,5", "--select-right", "6,6,6", "--pairs", "8",
        "--collect-a", "true", "--collect-b", "true"};
    const auto config = parse(static_cast<int>(std::size(args)), args);
    check(config.atRisk == 0 && config.roll.size() == 6 && config.selectLeft.size() == 4 &&
          config.selectRight.size() == 3, "Selection CLI must parse the two exact face lists with zero pre-selection risk by default.");
    const auto output = run(config);
    check(output.find("\"schema_version\":2,\"mode\":\"selection\"") != std::string::npos &&
          output.find("\"at_risk_before_selection\":0") != std::string::npos &&
          output.find("\"saved_multiple_scores\":[0,0,0,0,0,600]") != std::string::npos &&
          output.find("\"incumbent_recommendation\":") != std::string::npos &&
          output.find("\"right_minus_left_match_points_paired\":") != std::string::npos,
          "Evidence must include schema, pre-selection score, saved chains, incumbent choice and paired raw moments.");
    const char* illegal[] = {"zilch_research", "--mode", "duel", "--roll", "6,6,6,5,2,3"};
    bool rejected = false;
    try {
        static_cast<void>(parse(static_cast<int>(std::size(illegal)), illegal));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "The parser must not silently ignore selection flags in duel mode.");
    const char* features[] = {"zilch_research", "--chain-risk-a", "1.25", "--chain-mode-a", "blend",
        "--safe-finish-a", "true", "--chain-risk-b", "0.5"};
    const auto experimental = parse(static_cast<int>(std::size(features)), features);
    check(experimental.featuresA.chainRiskWeight == 1.25 && experimental.featuresA.lowerChainThresholds &&
          experimental.featuresA.safeFinishCollection && experimental.featuresB.chainRiskWeight == 0.5 &&
          !experimental.featuresB.lowerChainThresholds && !experimental.featuresB.safeFinishCollection,
          "Research feature flags must be independently scoped to each policy and disabled unless requested.");
}

void pairedIdentityAndThreadSeeds()
{
    auto config = selectionConfig();
    auto serial = run(config);
    config.threads = 4;
    const auto parallel = run(config);
    const auto field = serial.find("\"threads\":1");
    check(field != std::string::npos, "Serial output must record one worker.");
    serial.replace(field, std::string("\"threads\":1").size(), "\"threads\":4");
    check(serial == parallel, "Scheduling must not change any paired outcomes or raw moments.");
    config.selectRight = config.selectLeft;
    const auto same = run(config);
    check(same.find("\"right_minus_left_match_points_paired\":{\"independent_pairs\":64,\"sum\":0,\"sum_squares\":0,\"mean\":0") != std::string::npos &&
          same.find("\"right_minus_left_score_margin_paired\":{\"independent_pairs\":64,\"sum\":0,\"sum_squares\":0,\"mean\":0") != std::string::npos,
          "Identical branches must share the same dice stream and produce zero paired treatment differences.");
    auto low = engine(123);
    auto high = engine((std::uint64_t{1} << 32) + 123);
    check(low != high, "Both halves of a 64-bit seed must influence the pair's dice generator.");
}

void exclusiveEvidenceWrite()
{
    std::filesystem::path directory;
    std::random_device random;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = std::filesystem::temp_directory_path() /
            ("zilch-selection-cli-test-" + std::to_string(random()));
        if (std::filesystem::create_directory(candidate)) {
            directory = std::move(candidate);
            break;
        }
    }
    check(!directory.empty(), "Test must create a unique evidence directory.");
    const auto path = directory / "result.json";
    try {
        saveResult(path, "original evidence\n");
        bool rejected = false;
        try {
            saveResult(path, "replacement\n");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        std::ifstream input(path);
        std::string line;
        std::getline(input, line);
        check(rejected && line == "original evidence", "Research output must refuse replacement and preserve original evidence bytes.");
        std::filesystem::remove(path);
        std::filesystem::remove(directory);
    } catch (...) {
        std::filesystem::remove(path);
        std::filesystem::remove(directory);
        throw;
    }
}

} // namespace

int main()
{
    try {
        parserAndMetadata();
        pairedIdentityAndThreadSeeds();
        exclusiveEvidenceWrite();
        std::cout << "Selection CLI, paired seeds, metadata, and no-overwrite tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
