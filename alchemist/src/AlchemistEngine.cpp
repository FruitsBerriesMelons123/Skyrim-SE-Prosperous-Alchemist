#include "main.h"
#include "AlchemistEngine.h"

#include <Windows.h>

#include <mutex>
#include <condition_variable>
#include <thread>
#include <cmath>
#include <iomanip>
#include <cctype>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <array>
#include <chrono>
#include <algorithm>


namespace alchemist::engine {
	namespace {
		std::mutex snapshotMutex;
		constexpr std::size_t maxCachedRecipes = 2500;
		vector<RecipeResult> cachedRecipes;
		vector<RecipeResult> allCachedRecipes;
		std::size_t totalAvailableRecipes = 0;
		bool loadAllRequested = false;

		std::mutex progressMutex;
		CalculationProgress currentProgress{};
		std::atomic<std::uint64_t> cacheGeneration{ 1 };

		string FormatIngredients(const Potion& potion)
		{
			if (potion.size == 2) {
				return str::printSort2(potion.ingredient1.name, potion.ingredient2.name);
			}
			if (potion.size == 3) {
				return str::printSort3(potion.ingredient1.name, potion.ingredient2.name, potion.ingredient3.name);
			}
			return {};
		}

		string FormatIngredientDetails(const Potion& potion)
		{
			std::ostringstream result;
			// Recipe details describe one craft, not the player's available inventory quantity.
			const auto append = [&result](const Ingredient& ingredient) {
				if (!ingredient.nativeIngredient) {
					return;
				}
				if (result.tellp() > 0) {
					result << "; ";
				}
				result << ingredient.name << " [form=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
					<< ingredient.nativeIngredient->GetFormID() << std::dec << std::setfill(' ') << ", count=1]";
			};
			append(potion.ingredient1);
			append(potion.ingredient2);
			if (potion.size == 3) {
				append(potion.ingredient3);
			}
			return result.str();
		}

		std::vector<std::uint32_t> GetIngredientFormIDs(const Potion& potion)
		{
			std::vector<std::uint32_t> formIDs;
			const auto append = [&formIDs](const Ingredient& ingredient) {
				if (ingredient.nativeIngredient) {
					formIDs.push_back(ingredient.nativeIngredient->GetFormID());
				}
			};
			append(potion.ingredient1);
			append(potion.ingredient2);
			if (potion.size == 3) {
				append(potion.ingredient3);
			}
			return formIDs;
		}

		string FormatIngredientDetails(const vector<Ingredient>& ingredients)
		{
			std::ostringstream result;
			for (const auto& ingredient : ingredients) {
				if (!ingredient.nativeIngredient) {
					continue;
				}
				if (result.tellp() > 0) {
					result << "; ";
				}
				result << ingredient.name << " [form=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
					<< ingredient.nativeIngredient->GetFormID() << std::dec << std::setfill(' ') << ", count=1]";
			}
			return result.str();
		}

		string FormatEffects(const EffectList& effects, const Effect& controlEffect)
		{
			string result;
			for (const auto& effect : effects) {
				if (!result.empty()) {
					result += "\n";
				}
				result += effect::getPerkCalcDescription(effect, controlEffect.beneficial);
			}
			return result;
		}

		string FormatEffects(const Potion& potion)
		{
			return FormatEffects(potion.effects, potion.controlEffect);
		}

		string FormatEffects(const NativePotionResult& result)
		{
			return FormatEffects(result.effects, result.controlEffect);
		}

		string FormatCalculationDetails(const EffectList& effects)
		{
			std::ostringstream result;
			bool first = true;
			for (const auto& effect : effects) {
				if (!first) {
					result << "; ";
				}
				first = false;
				result << effect.name
					<< " source_form_id=" << (effect.sourceBaseEffect ? effect.sourceBaseEffect->GetFormID() : 0)
					<< " resolved_form_id=" << (effect.baseEffect ? effect.baseEffect->GetFormID() : 0)
					<< " beneficial=" << (effect.beneficial ? 1 : 0)
					<< " harmful=" << (effect.harmful ? 1 : 0)
					<< " hostile=" << (effect.hostile ? 1 : 0)
					<< " duration_based=" << (effect.durationBased ? 1 : 0)
					<< " base_cost=" << std::setprecision(9) << effect.baseCost
					<< " source_magnitude=" << effect.magnitude
					<< " predicted_magnitude=" << effect.calcMagnitude
					<< " source_duration=" << effect.duration
					<< " predicted_duration=" << effect.calcDuration
					<< " predicted_effect_cost=" << effect.calcCost
					<< " native_order_cost=" << effect.nativeOrderCost
					<< " native_cost=" << effect.nativeCost;
			}
			return result.str().empty() ? "unavailable" : result.str();
		}

		string FormatCalculationDetails(const NativePotionResult& result)
		{
			std::ostringstream details;
			details << "result_type=" << (result.isPoison ? "poison" : "potion")
				<< " has_beneficial=" << (result.hasBeneficial ? 1 : 0)
				<< " has_harmful=" << (result.hasHarmful ? 1 : 0)
				<< " pre_adjustment_gold=" << result.preAdjustmentGold
				<< " effects=" << FormatCalculationDetails(result.effects);
			return details.str();
		}

		string FormatCalculationDetails(const Potion& potion)
		{
			return FormatCalculationDetails(potion.effects);
		}

		bool ParseIngredientFormIDs(const std::string& details, std::vector<std::uint32_t>& formIDs)
		{
			formIDs.clear();
			std::size_t position = 0;
			while ((position = details.find("form=0x", position)) != std::string::npos) {
				position += 7;
				const auto first = position;
				while (position < details.size() && std::isxdigit(static_cast<unsigned char>(details[position]))) {
					++position;
				}
				if (first == position) {
					return false;
				}
				try {
					const auto value = std::stoull(details.substr(first, position - first), nullptr, 16);
					if (value > (std::numeric_limits<std::uint32_t>::max)()) {
						return false;
					}
					formIDs.push_back(static_cast<std::uint32_t>(value));
				} catch (...) {
					return false;
				}
			}
			return formIDs.size() == 2 || formIDs.size() == 3;
		}

		RecipeResult ToRecipeResult(const Potion& potion, bool isBest)
		{
			return RecipeResult{
			.name = potion.name,
			.effects = FormatEffects(potion),
			.ingredients = FormatIngredients(potion),
			.ingredientDetails = FormatIngredientDetails(potion),
			.ingredientFormIDs = GetIngredientFormIDs(potion),
			.calculationDetails = FormatCalculationDetails(potion),
			.calculatedValue = potion.cost,
			.displayedValue = static_cast<int>(std::floor(potion.cost)),
			.isBest = isBest
			};
		}

		struct RecalculationSnapshot {
			std::vector<Ingredient> ingredients;
			Player player;
			std::uint64_t cacoRevision = 0;
		};

		bool SnapshotsMatch(const RecalculationSnapshot& a, const RecalculationSnapshot& b)
		{
			return a.cacoRevision == b.cacoRevision &&
				a.player.state == b.player.state &&
				a.ingredients == b.ingredients;
		}

		RecalculationSnapshot CaptureSnapshot()
		{
			initAlchemist();

			RecalculationSnapshot snapshot;
			snapshot.ingredients.assign(ingredients.begin(), ingredients.end());
			snapshot.player = player;
			snapshot.cacoRevision = caco::Adapter::GetCalculationRevision();
			return snapshot;
		}

		struct MasterRecipeEntry {
			std::array<std::uint32_t, 3> formIDs{};
			std::array<RE::IngredientItem*, 3> nativeIngredients{};
			std::uint8_t formCount = 0;
			RecipeResult result;
		};

		struct MasterRecipeCache {
			std::vector<MasterRecipeEntry> entries;
			std::unordered_set<std::uint32_t> availableFormIDs;
			std::string playerState;
			std::uint64_t cacoRevision = 0;
			std::chrono::steady_clock::time_point generationTime;
			std::chrono::steady_clock::time_point menuClosedTime;
			std::uint32_t lastCalculationDurationMs = 0;
			bool isStale = false;
			std::string staleReason;
			bool hasMenuClosedTime = false;
			bool isValid = false;
		};

		std::mutex masterCacheMutex;
		MasterRecipeCache masterCache;

		bool IsMasterCacheExpiredLocked()
		{
			if (!masterCache.hasMenuClosedTime) {
				return false;
			}
			const auto timeoutSeconds = kCacheDurationSeconds.GetValue();
			if (timeoutSeconds <= 0) {
				return true;
			}
			const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::steady_clock::now() - masterCache.menuClosedTime).count();
			return elapsed > timeoutSeconds;
		}

		bool IsSubsetOfMasterCacheLocked(const std::unordered_set<std::uint32_t>& currentFormIDs)
		{
			if (!masterCache.isValid || masterCache.availableFormIDs.empty()) {
				return false;
			}
			for (const auto formID : currentFormIDs) {
				if (masterCache.availableFormIDs.find(formID) == masterCache.availableFormIDs.end()) {
					return false;
				}
			}
			return true;
		}

		std::vector<RecipeResult> FilterMasterCacheLocked(const std::unordered_set<std::uint32_t>& currentFormIDs)
		{
			std::vector<RecipeResult> filtered;
			filtered.reserve(masterCache.entries.size());
			bool isFirst = true;
			for (const auto& entry : masterCache.entries) {
				if (currentFormIDs.find(entry.formIDs[0]) != currentFormIDs.end() &&
					currentFormIDs.find(entry.formIDs[1]) != currentFormIDs.end() &&
					(entry.formCount < 3 || currentFormIDs.find(entry.formIDs[2]) != currentFormIDs.end()))
				{
					auto recipe = entry.result;
					recipe.isBest = isFirst;
					isFirst = false;
					filtered.push_back(std::move(recipe));
				}
			}
			return filtered;
		}

		std::vector<bool> FindNewIngredientMaskLocked(const std::vector<Ingredient>& ingredients)
		{
			std::vector<bool> mask(ingredients.size(), false);
			for (std::size_t i = 0; i < ingredients.size(); ++i) {
				if (ingredients[i].nativeIngredient) {
					const auto formID = ingredients[i].nativeIngredient->GetFormID();
					if (masterCache.availableFormIDs.find(formID) == masterCache.availableFormIDs.end()) {
						mask[i] = true;
					}
				}
			}
			return mask;
		}

		void ReevaluateMasterCacheAtPlayerStateLocked(
			const Player& player,
			bool multithreaded,
			const std::atomic<bool>* cancelToken)
		{
			const auto total = masterCache.entries.size();
			if (total == 0) {
				return;
			}

			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 0.05f;
				currentProgress.phase = "Re-evaluating recipes...";
				currentProgress.current = 0;
				currentProgress.total = total;
			}

			struct EvaluatedItem {
				std::size_t entryIndex = 0;
				NativePotionResult nativeResult;
			};

			const auto hardwareThreads = std::thread::hardware_concurrency();
			const std::size_t requestedWorkers = hardwareThreads > 0 ? hardwareThreads : 1;
			const std::size_t workerCount = multithreaded ? (std::min)(requestedWorkers, total) : 1;

			std::atomic<std::size_t> nextIndex = 0;
			std::vector<std::vector<EvaluatedItem>> workerResults(workerCount);

			const auto worker = [&](std::size_t workerIndex) {
				auto& results = workerResults[workerIndex];
				while (true) {
					if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
						break;
					}
					const auto idx = nextIndex.fetch_add(1, std::memory_order_relaxed);
					if (idx >= total) {
						break;
					}
					if ((idx & 0x7F) == 0 || idx + 1 >= total) {
						std::scoped_lock lock(progressMutex);
						currentProgress.isUpdating = true;
						currentProgress.progressFraction = 0.05f + 0.80f * (static_cast<float>(idx + 1) / static_cast<float>(total));
						currentProgress.phase = "Re-evaluating recipes...";
						currentProgress.current = idx + 1;
						currentProgress.total = total;
					}
					const auto& entry = masterCache.entries[idx];
					std::vector<Ingredient> selectedIngredients;
					selectedIngredients.reserve(entry.formCount);
					for (std::size_t f = 0; f < entry.formCount; ++f) {
						if (entry.nativeIngredients[f]) {
							selectedIngredients.emplace_back(entry.nativeIngredients[f]);
						}
					}
					if (selectedIngredients.size() == entry.formCount) {
						std::vector<const Ingredient*> pointers;
						pointers.reserve(selectedIngredients.size());
						for (const auto& ing : selectedIngredients) {
							pointers.push_back(&ing);
						}
						auto res = effect::evaluatePotion(pointers, player);
						if (res.valid && std::isfinite(res.cost) && static_cast<int>(std::floor(res.cost)) >= 1) {
							results.push_back({ idx, std::move(res) });
						}
					}
				}
			};

			if (workerCount == 1) {
				worker(0);
			} else {
				std::vector<std::thread> workers;
				workers.reserve(workerCount);
				for (std::size_t w = 0; w < workerCount; ++w) {
					workers.emplace_back(worker, w);
				}
				for (auto& w : workers) {
					w.join();
				}
			}

			if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = false;
				return;
			}

			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 0.88f;
				currentProgress.phase = "Sorting recipes...";
				currentProgress.current = total;
				currentProgress.total = total;
			}

			for (const auto& wr : workerResults) {
				for (const auto& item : wr) {
					auto& entry = masterCache.entries[item.entryIndex];
					entry.result.effects = FormatEffects(item.nativeResult);
					entry.result.calculationDetails = FormatCalculationDetails(item.nativeResult);
					entry.result.calculatedValue = item.nativeResult.cost;
					entry.result.displayedValue = static_cast<int>(std::floor(item.nativeResult.cost));
				}
			}

			std::sort(masterCache.entries.begin(), masterCache.entries.end(), [](const MasterRecipeEntry& left, const MasterRecipeEntry& right) {
				if (left.result.calculatedValue != right.result.calculatedValue) {
					return left.result.calculatedValue > right.result.calculatedValue;
				}
				return left.result.name < right.result.name;
			});

			masterCache.playerState = player.state;

			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 0.95f;
				currentProgress.phase = "Finalizing potion list...";
				currentProgress.current = total;
				currentProgress.total = total;
			}
		}

		void MergeDeltaMasterCacheLocked(
			const RecalculationSnapshot& snapshot,
			std::vector<MasterRecipeEntry> deltaEntries)
		{
			for (const auto& ing : snapshot.ingredients) {
				if (ing.nativeIngredient) {
					masterCache.availableFormIDs.insert(ing.nativeIngredient->GetFormID());
				}
			}
			masterCache.playerState = snapshot.player.state;
			masterCache.cacoRevision = snapshot.cacoRevision;
			masterCache.hasMenuClosedTime = false;
			masterCache.isValid = true;

			if (deltaEntries.empty()) {
				return;
			}

			std::sort(deltaEntries.begin(), deltaEntries.end(), [](const MasterRecipeEntry& left, const MasterRecipeEntry& right) {
				if (left.result.calculatedValue != right.result.calculatedValue) {
					return left.result.calculatedValue > right.result.calculatedValue;
				}
				return left.result.name < right.result.name;
			});

			std::vector<MasterRecipeEntry> merged;
			merged.reserve(masterCache.entries.size() + deltaEntries.size());

			auto itOld = masterCache.entries.begin();
			auto itDelta = deltaEntries.begin();

			while (itOld != masterCache.entries.end() && itDelta != deltaEntries.end()) {
				if (itDelta->result.calculatedValue > itOld->result.calculatedValue) {
					merged.push_back(std::move(*itDelta));
					++itDelta;
				} else {
					merged.push_back(std::move(*itOld));
					++itOld;
				}
			}
			while (itOld != masterCache.entries.end()) {
				merged.push_back(std::move(*itOld));
				++itOld;
			}
			while (itDelta != deltaEntries.end()) {
				merged.push_back(std::move(*itDelta));
				++itDelta;
			}

			masterCache.entries = std::move(merged);
		}

		void PopulateMasterCache(const RecalculationSnapshot& snapshot, std::vector<MasterRecipeEntry> entries)
		{
			std::scoped_lock cacheLock(masterCacheMutex);
			masterCache.entries = std::move(entries);
			masterCache.availableFormIDs.clear();
			for (const auto& ing : snapshot.ingredients) {
				if (ing.nativeIngredient) {
					masterCache.availableFormIDs.insert(ing.nativeIngredient->GetFormID());
				}
			}
			masterCache.playerState = snapshot.player.state;
			masterCache.cacoRevision = snapshot.cacoRevision;
			masterCache.generationTime = std::chrono::steady_clock::now();
			masterCache.hasMenuClosedTime = false;
			masterCache.isValid = true;
		}

		struct GeneratedOutput {
			std::vector<RecipeResult> results;
			std::vector<MasterRecipeEntry> masterEntries;
		};

		GeneratedOutput GenerateRecipeResults(
			const RecalculationSnapshot& snapshot,
			bool multithreaded,
			const std::atomic<bool>* cancelToken,
			const std::vector<bool>* isNewIngredient = nullptr)
		{
			auto progressCallback = [](float fraction, const char* phase, std::size_t current, std::size_t total) {
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = std::clamp(fraction, 0.0f, 1.0f);
				currentProgress.phase = phase ? phase : "";
				currentProgress.current = current;
				currentProgress.total = total;
			};

			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 0.0f;
				currentProgress.phase = "Starting calculation";
				currentProgress.current = 0;
				currentProgress.total = 0;
			}

			auto calculation = CalculateRecipesFromSnapshot(
				snapshot.ingredients, snapshot.player, multithreaded, cancelToken, progressCallback, isNewIngredient);

			if (calculation.cancelled || (cancelToken && cancelToken->load(std::memory_order_relaxed))) {
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = false;
				return {};
			}

			std::vector<const Potion*> sortedPotions;
			sortedPotions.reserve(calculation.potions.size());
			for (const auto& potion : calculation.potions) {
				if (potion.size > 0 && static_cast<int>(std::floor(potion.cost)) >= 1) {
					sortedPotions.push_back(&potion);
				}
			}

			const std::size_t totalPotions = sortedPotions.size();
			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 0.88f;
				currentProgress.phase = "Sorting recipes...";
				currentProgress.current = 0;
				currentProgress.total = totalPotions;
			}

			std::sort(sortedPotions.begin(), sortedPotions.end(), [](const Potion* left, const Potion* right) {
				if (left->cost != right->cost) {
					return left->cost > right->cost;
				}
				return left->id < right->id;
			});

			if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = false;
				return {};
			}

			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 0.90f;
				currentProgress.phase = "Formatting recipes...";
				currentProgress.current = 0;
				currentProgress.total = totalPotions;
			}

			std::vector<RecipeResult> results;
			std::vector<MasterRecipeEntry> masterEntries;
			results.reserve(totalPotions);
			masterEntries.reserve(totalPotions);
			for (std::size_t i = 0; i < totalPotions; ++i) {
				if ((i & 0x3F) == 0 || i + 1 == totalPotions) {
					if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
						std::scoped_lock lock(progressMutex);
						currentProgress.isUpdating = false;
						return {};
					}
					if (totalPotions > 0) {
						const float fraction = 0.90f + 0.10f * (static_cast<float>(i + 1) / static_cast<float>(totalPotions));
						std::scoped_lock lock(progressMutex);
						currentProgress.isUpdating = true;
						currentProgress.progressFraction = std::clamp(fraction, 0.0f, 1.0f);
						currentProgress.phase = "Formatting recipes...";
						currentProgress.current = i + 1;
						currentProgress.total = totalPotions;
					}
				}

				const auto* potion = sortedPotions[i];
				auto ingredientStr = FormatIngredients(*potion);
				if (!ingredientStr.empty()) {
					MasterRecipeEntry entry;
					entry.nativeIngredients[0] = potion->ingredient1.nativeIngredient;
					entry.nativeIngredients[1] = potion->ingredient2.nativeIngredient;
					entry.formIDs[0] = potion->ingredient1.nativeIngredient ? potion->ingredient1.nativeIngredient->GetFormID() : 0;
					entry.formIDs[1] = potion->ingredient2.nativeIngredient ? potion->ingredient2.nativeIngredient->GetFormID() : 0;
					entry.formCount = 2;
					if (potion->size == 3) {
						entry.nativeIngredients[2] = potion->ingredient3.nativeIngredient;
						entry.formIDs[2] = potion->ingredient3.nativeIngredient ? potion->ingredient3.nativeIngredient->GetFormID() : 0;
						entry.formCount = 3;
					} else {
						entry.nativeIngredients[2] = nullptr;
						entry.formIDs[2] = 0;
					}
					entry.result = RecipeResult{
						.name = potion->name,
						.effects = FormatEffects(*potion),
						.ingredients = std::move(ingredientStr),
						.ingredientDetails = FormatIngredientDetails(*potion),
						.ingredientFormIDs = GetIngredientFormIDs(*potion),
						.calculationDetails = FormatCalculationDetails(*potion),
						.calculatedValue = potion->cost,
						.displayedValue = static_cast<int>(std::floor(potion->cost)),
						.isBest = (potion->id == calculation.costliestPotion.id)
					};
					results.push_back(entry.result);
					masterEntries.push_back(std::move(entry));
				}
			}

			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 1.0f;
				currentProgress.phase = "Finalizing potion list...";
				currentProgress.current = totalPotions;
				currentProgress.total = totalPotions;
			}

			return GeneratedOutput{ std::move(results), std::move(masterEntries) };
		}

		struct AsyncJob {
			RecalculationSnapshot snapshot;
			std::vector<std::function<void()>> callbacks;
			std::shared_ptr<std::atomic<bool>> cancelToken;
			bool force = false;
			std::chrono::steady_clock::time_point queueTime{};
		};

		std::atomic<bool> isRecipeListStale{ false };
		std::mutex staleMutex;
		std::string staleReasonText;
		std::atomic<std::uint32_t> lastCalculationDurationMs{ 0 };

		std::mutex workerMutex;
		std::condition_variable workerCv;
		std::thread persistentWorker;
		bool workerStarted = false;
		bool workerStopping = false;
		std::shared_ptr<AsyncJob> currentActiveJob;
		std::shared_ptr<AsyncJob> pendingJob;
		RecalculationSnapshot lastCompletedSnapshot;
		bool hasCompletedSnapshot = false;

		void ExecuteJob(const std::shared_ptr<AsyncJob>& job)
		{
			if (!job || (job->cancelToken && job->cancelToken->load(std::memory_order_relaxed))) {
				return;
			}

			const auto startTime = std::chrono::steady_clock::now();
			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				currentProgress.progressFraction = 0.05f;
				currentProgress.phase = "Recalculating recipes...";
				currentProgress.current = 0;
				currentProgress.total = 0;
			}
			std::vector<RecipeResult> results;
			bool isDeltaMerge = false;
			std::vector<MasterRecipeEntry> deltaMasterEntries;
			std::vector<MasterRecipeEntry> fullMasterEntries;
			std::unordered_set<std::uint32_t> newFormIDs;

			std::unordered_set<std::uint32_t> currentFormIDs;
			for (const auto& ing : job->snapshot.ingredients) {
				if (ing.nativeIngredient) {
					currentFormIDs.insert(ing.nativeIngredient->GetFormID());
				}
			}

			bool handled = false;

			{
				std::scoped_lock cacheLock(masterCacheMutex);
				if (masterCache.isValid && !IsMasterCacheExpiredLocked() && job->snapshot.cacoRevision == masterCache.cacoRevision) {
					for (const auto fid : currentFormIDs) {
						if (masterCache.availableFormIDs.find(fid) == masterCache.availableFormIDs.end()) {
							newFormIDs.insert(fid);
						}
					}

					if (newFormIDs.empty()) {
						if (job->snapshot.player.state == masterCache.playerState) {
							{
								std::scoped_lock lock(progressMutex);
								currentProgress.isUpdating = true;
								currentProgress.progressFraction = 0.50f;
								currentProgress.phase = "Filtering recipe cache...";
							}
							results = FilterMasterCacheLocked(currentFormIDs);
							handled = true;
						} else {
							const bool multithreaded = kSinglethreaded.GetValue() == 0;
							ReevaluateMasterCacheAtPlayerStateLocked(
								job->snapshot.player, multithreaded, job->cancelToken.get());
							{
								std::scoped_lock lock(progressMutex);
								currentProgress.isUpdating = true;
								currentProgress.progressFraction = 0.95f;
								currentProgress.phase = "Filtering recipe cache...";
							}
							results = FilterMasterCacheLocked(currentFormIDs);
							handled = true;
						}
					} else {
						if (job->snapshot.player.state != masterCache.playerState) {
							const bool multithreaded = kSinglethreaded.GetValue() == 0;
							ReevaluateMasterCacheAtPlayerStateLocked(
								job->snapshot.player, multithreaded, job->cancelToken.get());
						}
						isDeltaMerge = true;
					}
				}
			}

			if (isDeltaMerge) {
				const bool multithreaded = kSinglethreaded.GetValue() == 0;
				std::vector<bool> isNew(job->snapshot.ingredients.size(), false);
				for (std::size_t i = 0; i < job->snapshot.ingredients.size(); ++i) {
					if (job->snapshot.ingredients[i].nativeIngredient) {
						const auto fid = job->snapshot.ingredients[i].nativeIngredient->GetFormID();
						if (newFormIDs.find(fid) != newFormIDs.end()) {
							isNew[i] = true;
						}
					}
				}

				auto progressCallback = [](float fraction, const char* phase, std::size_t current, std::size_t total) {
					std::scoped_lock lock(progressMutex);
					currentProgress.isUpdating = true;
					currentProgress.progressFraction = std::clamp(fraction, 0.0f, 1.0f);
					currentProgress.phase = phase ? phase : "";
					currentProgress.current = current;
					currentProgress.total = total;
				};

				{
					std::scoped_lock lock(progressMutex);
					currentProgress.isUpdating = true;
					currentProgress.progressFraction = 0.0f;
					currentProgress.phase = "Evaluating new ingredient combinations...";
					currentProgress.current = 0;
					currentProgress.total = 0;
				}

				auto calcOutput = CalculateRecipesFromSnapshot(
					job->snapshot.ingredients, job->snapshot.player, multithreaded,
					job->cancelToken.get(), progressCallback, &isNew);

				if (calcOutput.cancelled || (job->cancelToken && job->cancelToken->load(std::memory_order_relaxed))) {
					std::scoped_lock lock(progressMutex);
					currentProgress.isUpdating = false;
					return;
				}

				deltaMasterEntries.reserve(calcOutput.potions.size());
				for (const auto& potion : calcOutput.potions) {
					if (potion.size > 0 && static_cast<int>(std::floor(potion.cost)) >= 1) {
						auto ingredientStr = FormatIngredients(potion);
						if (!ingredientStr.empty()) {
							MasterRecipeEntry entry;
							entry.nativeIngredients[0] = potion.ingredient1.nativeIngredient;
							entry.nativeIngredients[1] = potion.ingredient2.nativeIngredient;
							entry.formIDs[0] = potion.ingredient1.nativeIngredient ? potion.ingredient1.nativeIngredient->GetFormID() : 0;
							entry.formIDs[1] = potion.ingredient2.nativeIngredient ? potion.ingredient2.nativeIngredient->GetFormID() : 0;
							entry.formCount = 2;
							if (potion.size == 3) {
								entry.nativeIngredients[2] = potion.ingredient3.nativeIngredient;
								entry.formIDs[2] = potion.ingredient3.nativeIngredient ? potion.ingredient3.nativeIngredient->GetFormID() : 0;
								entry.formCount = 3;
							} else {
								entry.nativeIngredients[2] = nullptr;
								entry.formIDs[2] = 0;
							}
							entry.result = RecipeResult{
								.name = potion.name,
								.effects = FormatEffects(potion),
								.ingredients = std::move(ingredientStr),
								.ingredientDetails = FormatIngredientDetails(potion),
						.ingredientFormIDs = GetIngredientFormIDs(potion),
								.calculationDetails = FormatCalculationDetails(potion),
								.calculatedValue = potion.cost,
								.displayedValue = static_cast<int>(std::floor(potion.cost)),
								.isBest = false
							};
							deltaMasterEntries.push_back(std::move(entry));
						}
					}
				}

				{
					std::scoped_lock cacheLock(masterCacheMutex);
					MergeDeltaMasterCacheLocked(job->snapshot, std::move(deltaMasterEntries));
					results = FilterMasterCacheLocked(currentFormIDs);
				}
				handled = true;
			}

			if (!handled) {
				const bool multithreaded = kSinglethreaded.GetValue() == 0;
				auto generated = GenerateRecipeResults(job->snapshot, multithreaded, job->cancelToken.get());
				if (job->cancelToken && job->cancelToken->load(std::memory_order_relaxed)) {
					std::scoped_lock lock(progressMutex);
					currentProgress.isUpdating = false;
					return;
				}
				results = std::move(generated.results);
				fullMasterEntries = std::move(generated.masterEntries);
				PopulateMasterCache(job->snapshot, std::move(fullMasterEntries));
			}

			const bool cancelled = job->cancelToken->load(std::memory_order_relaxed);
			if (cancelled) {
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = false;
				return;
			}

			const auto endTime = std::chrono::steady_clock::now();
			const auto durationMs = static_cast<std::uint32_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
			lastCalculationDurationMs.store(durationMs, std::memory_order_release);

			{
				std::scoped_lock lock(staleMutex);
				isRecipeListStale.store(false, std::memory_order_release);
				staleReasonText.clear();
			}

			{
				std::lock_guard snapLock(snapshotMutex);
				loadAllRequested = true;
				totalAvailableRecipes = results.size();
				cachedRecipes = std::move(results);
				allCachedRecipes.clear();
				cacheGeneration.fetch_add(1, std::memory_order_release);
				lastCompletedSnapshot = job->snapshot;
				hasCompletedSnapshot = true;
			}

			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = false;
				currentProgress.progressFraction = 1.0f;
				currentProgress.phase = "Completed";
			}

			for (const auto& cb : job->callbacks) {
				if (cb) {
					cb();
				}
			}
		}

		void WorkerLoop()
		{
			while (true) {
				std::shared_ptr<AsyncJob> job;
				{
					std::unique_lock lock(workerMutex);
					while (!workerStopping && !pendingJob) {
						workerCv.wait(lock);
					}
					if (workerStopping) {
						break;
					}

					const auto debounceMs = kCraftDebounceMs.GetValue();
					if (!pendingJob->force && debounceMs > 0) {
						while (true) {
							const auto now = std::chrono::steady_clock::now();
							const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pendingJob->queueTime).count();
							if (elapsed >= debounceMs) {
								break;
							}
							const auto remaining = debounceMs - elapsed;
							workerCv.wait_for(lock, std::chrono::milliseconds(remaining));
							if (workerStopping) {
								return;
							}
						}
					}

					job = std::move(pendingJob);
					pendingJob.reset();
					currentActiveJob = job;
				}

				if (job) {
					ExecuteJob(job);
					{
						std::scoped_lock lock(workerMutex);
						if (currentActiveJob == job) {
							currentActiveJob.reset();
						}
					}
				}
			}
		}

		void EnsureWorkerStarted()
		{
			if (!workerStarted) {
				workerStarted = true;
				workerStopping = false;
				persistentWorker = std::thread(WorkerLoop);
				persistentWorker.detach();
			}
		}
	}

	void Recalculate(bool a_force)
	{
		auto snapshot = CaptureSnapshot();

		{
			std::scoped_lock lock(snapshotMutex);
			if (!a_force && hasCompletedSnapshot && SnapshotsMatch(snapshot, lastCompletedSnapshot)) {
				return;
			}
		}

		std::unordered_set<std::uint32_t> currentFormIDs;
		for (const auto& ing : snapshot.ingredients) {
			if (ing.nativeIngredient) {
				currentFormIDs.insert(ing.nativeIngredient->GetFormID());
			}
		}

		{
			std::scoped_lock cacheLock(masterCacheMutex);
			if (masterCache.isValid && !IsMasterCacheExpiredLocked() && snapshot.cacoRevision == masterCache.cacoRevision) {
				const bool isSubset = IsSubsetOfMasterCacheLocked(currentFormIDs);
				const bool stateMatches = (snapshot.player.state == masterCache.playerState);

				if (isSubset && stateMatches) {
					auto filtered = FilterMasterCacheLocked(currentFormIDs);
					{
						std::lock_guard snapLock(snapshotMutex);
						loadAllRequested = true;
						totalAvailableRecipes = filtered.size();
						cachedRecipes = std::move(filtered);
						allCachedRecipes.clear();
						cacheGeneration.fetch_add(1, std::memory_order_release);
						lastCompletedSnapshot = std::move(snapshot);
						hasCompletedSnapshot = true;
					}
					{
						std::scoped_lock lock(progressMutex);
						currentProgress.isUpdating = false;
						currentProgress.progressFraction = 1.0f;
						currentProgress.phase = "Completed";
					}
					{
						std::scoped_lock wLock(workerMutex);
						if (pendingJob) {
							pendingJob->cancelToken->store(true, std::memory_order_relaxed);
							pendingJob.reset();
						}
					}
					return;
				}
			}
		}

		{
			std::scoped_lock wLock(workerMutex);
			if (pendingJob) {
				pendingJob->cancelToken->store(true, std::memory_order_relaxed);
				pendingJob.reset();
			}
			if (currentActiveJob) {
				currentActiveJob->cancelToken->store(true, std::memory_order_relaxed);
			}
		}

		auto job = std::make_shared<AsyncJob>();
		job->snapshot = std::move(snapshot);
		job->force = a_force;
		job->cancelToken = std::make_shared<std::atomic<bool>>(false);

		ExecuteJob(job);
	}

	void RecalculateAsync(std::function<void()> a_onComplete, bool a_force)
	{
		auto snapshot = CaptureSnapshot();

		{
			std::scoped_lock lock(snapshotMutex);
			if (!a_force && hasCompletedSnapshot && SnapshotsMatch(snapshot, lastCompletedSnapshot)) {
				if (a_onComplete) {
					a_onComplete();
				}
				return;
			}
		}

		std::unordered_set<std::uint32_t> currentFormIDs;
		for (const auto& ing : snapshot.ingredients) {
			if (ing.nativeIngredient) {
				currentFormIDs.insert(ing.nativeIngredient->GetFormID());
			}
		}

		const auto thresholdMs = kStaleRecalculateThresholdMs.GetValue();
		const bool guardActive = (thresholdMs > 0) &&
			(lastCalculationDurationMs.load(std::memory_order_acquire) > static_cast<std::uint32_t>(thresholdMs));

		{
			std::scoped_lock cacheLock(masterCacheMutex);
			if (masterCache.isValid && !IsMasterCacheExpiredLocked() && snapshot.cacoRevision == masterCache.cacoRevision) {
				const bool isSubset = IsSubsetOfMasterCacheLocked(currentFormIDs);
				const bool stateMatches = (snapshot.player.state == masterCache.playerState);

				if (isSubset && stateMatches) {
					auto filtered = FilterMasterCacheLocked(currentFormIDs);
					{
						std::lock_guard snapLock(snapshotMutex);
						loadAllRequested = true;
						totalAvailableRecipes = filtered.size();
						cachedRecipes = std::move(filtered);
						allCachedRecipes.clear();
						cacheGeneration.fetch_add(1, std::memory_order_release);
					}
					{
						std::scoped_lock lock(progressMutex);
						currentProgress.isUpdating = false;
						currentProgress.progressFraction = 1.0f;
						currentProgress.phase = "Completed";
					}
					{
						std::scoped_lock wLock(workerMutex);
						if (pendingJob) {
							pendingJob->cancelToken->store(true, std::memory_order_relaxed);
							pendingJob.reset();
						}
					}
					{
						std::scoped_lock snapLock(snapshotMutex);
						lastCompletedSnapshot = std::move(snapshot);
						hasCompletedSnapshot = true;
					}
					if (a_onComplete) {
						a_onComplete();
					}
					return;
				}

				if (!a_force) {
					if (isSubset && !stateMatches) {
						auto filtered = FilterMasterCacheLocked(currentFormIDs);
						{
							std::lock_guard snapLock(snapshotMutex);
							loadAllRequested = true;
							totalAvailableRecipes = filtered.size();
							cachedRecipes = std::move(filtered);
							allCachedRecipes.clear();
							cacheGeneration.fetch_add(1, std::memory_order_release);
						}
						if (guardActive) {
							{
								std::scoped_lock lock(progressMutex);
								currentProgress.isUpdating = false;
								currentProgress.progressFraction = 1.0f;
								currentProgress.phase = "Completed";
							}
							{
								std::scoped_lock sLock(staleMutex);
								isRecipeListStale.store(true, std::memory_order_release);
								staleReasonText = "Alchemy level changed";
							}
							{
								std::scoped_lock wLock(workerMutex);
								if (pendingJob) {
									pendingJob->cancelToken->store(true, std::memory_order_relaxed);
									pendingJob.reset();
								}
							}
							{
								std::scoped_lock snapLock(snapshotMutex);
								lastCompletedSnapshot = std::move(snapshot);
								hasCompletedSnapshot = true;
							}
							if (a_onComplete) {
								a_onComplete();
							}
							return;
						}
					}

					if (!isSubset && guardActive) {
						auto filtered = FilterMasterCacheLocked(currentFormIDs);
						{
							std::lock_guard snapLock(snapshotMutex);
							loadAllRequested = true;
							totalAvailableRecipes = filtered.size();
							cachedRecipes = std::move(filtered);
							allCachedRecipes.clear();
							cacheGeneration.fetch_add(1, std::memory_order_release);
						}
						{
							std::scoped_lock lock(progressMutex);
							currentProgress.isUpdating = false;
							currentProgress.progressFraction = 1.0f;
							currentProgress.phase = "Completed";
						}
						{
							std::scoped_lock sLock(staleMutex);
							isRecipeListStale.store(true, std::memory_order_release);
							staleReasonText = "New ingredients available";
						}
						{
							std::scoped_lock wLock(workerMutex);
							if (pendingJob) {
								pendingJob->cancelToken->store(true, std::memory_order_relaxed);
								pendingJob.reset();
							}
						}
						{
							std::scoped_lock snapLock(snapshotMutex);
							lastCompletedSnapshot = std::move(snapshot);
							hasCompletedSnapshot = true;
						}
						if (a_onComplete) {
							a_onComplete();
						}
						return;
					}
				}
			}
		}

		{
			std::scoped_lock wLock(workerMutex);
			EnsureWorkerStarted();

			if (currentActiveJob) {
				currentActiveJob->cancelToken->store(true, std::memory_order_relaxed);
			}

			if (pendingJob) {
				pendingJob->snapshot = std::move(snapshot);
				pendingJob->force = pendingJob->force || a_force;
				pendingJob->queueTime = std::chrono::steady_clock::now();
				if (a_onComplete) {
					pendingJob->callbacks.push_back(std::move(a_onComplete));
				}
			} else {
				pendingJob = std::make_shared<AsyncJob>();
				pendingJob->snapshot = std::move(snapshot);
				pendingJob->force = a_force;
				pendingJob->cancelToken = std::make_shared<std::atomic<bool>>(false);
				pendingJob->queueTime = std::chrono::steady_clock::now();
				if (a_onComplete) {
					pendingJob->callbacks.push_back(std::move(a_onComplete));
				}
			}
			{
				std::scoped_lock lock(progressMutex);
				currentProgress.isUpdating = true;
				if (currentProgress.progressFraction < 0.05f) {
					currentProgress.progressFraction = 0.05f;
				}
				if (currentProgress.phase.empty() || currentProgress.phase == "Completed") {
					currentProgress.phase = "Recalculating recipes...";
				}
			}
		}
		workerCv.notify_one();
	}


	vector<RecipeResult> GetCachedRecipes()
	{
		std::lock_guard lock(snapshotMutex);
		return cachedRecipes;
	}

	std::size_t GetTotalAvailableRecipes()
	{
		std::lock_guard snapLock(snapshotMutex);
		return totalAvailableRecipes;
	}

	bool IsRecipeListIncomplete()
	{
		return false;
	}

	void LoadAllCachedRecipes()
	{
		std::lock_guard snapLock(snapshotMutex);
		if (!allCachedRecipes.empty()) {
			cachedRecipes = std::move(allCachedRecipes);
			allCachedRecipes.clear();
			cacheGeneration.fetch_add(1, std::memory_order_release);
		}
		loadAllRequested = true;
	}

	std::uint64_t GetRecipeCacheGeneration()
	{
		return cacheGeneration.load(std::memory_order_acquire);
	}

	CalculationProgress GetCalculationProgress()
	{
		std::scoped_lock lock(progressMutex);
		return currentProgress;
	}

	AlgorithmMatrixResult RunAlgorithmMatrix()
	{
		AlgorithmMatrixResult result;
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return result;
		}
		result.alchemyPlusActive = alchemyplus::Adapter::IsActive();
		result.cacoActive = caco::Adapter::IsActive();
		result.combinedActive = result.alchemyPlusActive && result.cacoActive;

		std::vector<Ingredient> availableIngredients;
		for (auto* nativeIngredient : dataHandler->GetFormArray<IngredientItem>()) {
			if (nativeIngredient && !nativeIngredient->IsDeleted()) {
				availableIngredients.emplace_back(nativeIngredient);
			}
		}
		result.ingredientCount = availableIngredients.size();

		const std::array<EvaluationAlgorithm, 4> algorithms{
			EvaluationAlgorithm::Vanilla,
			EvaluationAlgorithm::AlchemyPlus,
			EvaluationAlgorithm::CACO,
			EvaluationAlgorithm::Automatic
		};
		result.algorithms.reserve(algorithms.size());
		for (const auto algorithm : algorithms) {
			AlgorithmTestResult algorithmResult;
			algorithmResult.algorithm = algorithm;
			algorithmResult.ingredientCount = availableIngredients.size();
			algorithmResult.pairCandidates = availableIngredients.size() > 1 ?
				availableIngredients.size() * (availableIngredients.size() - 1) / 2 : 0;
			algorithmResult.tripleCandidates = availableIngredients.size() > 2 ?
				availableIngredients.size() * (availableIngredients.size() - 1) * (availableIngredients.size() - 2) / 6 : 0;
			algorithmResult.liveCompatibilityRecords = algorithm == EvaluationAlgorithm::AlchemyPlus ?
				result.alchemyPlusActive : algorithm == EvaluationAlgorithm::CACO ? result.cacoActive :
				algorithm == EvaluationAlgorithm::Automatic && (result.alchemyPlusActive || result.cacoActive);

			const bool useCacoNative = algorithm == EvaluationAlgorithm::CACO && caco::Adapter::IsActive();
			std::vector<std::vector<const RE::EffectSetting*>> effectIdentities;
			effectIdentities.reserve(availableIngredients.size());
			for (const auto& ingredient : availableIngredients) {
				std::vector<const RE::EffectSetting*> identities;
				identities.reserve(ingredient.effects.size());
				for (std::size_t effectIndex = 0; effectIndex < ingredient.effects.size(); ++effectIndex) {
					const auto effect = effect::getAlgorithmEffect(ingredient, effectIndex);
					const auto* identity = effect::getSourceIdentity(effect);
					if (identity && std::find(identities.begin(), identities.end(), identity) == identities.end()) {
						identities.push_back(identity);
					}
				}
				effectIdentities.push_back(std::move(identities));
			}

			const auto shareEffect = [&effectIdentities](std::size_t first, std::size_t second) {
				for (const auto* identity : effectIdentities[first]) {
					if (std::find(effectIdentities[second].begin(), effectIdentities[second].end(), identity) != effectIdentities[second].end()) {
						return true;
					}
				}
				return false;
			};
			std::vector<const Ingredient*> selectedIngredients;
			selectedIngredients.reserve(3);
			const auto evaluateResult = [&](std::size_t first, std::size_t second, std::size_t third) {
				selectedIngredients.clear();
				selectedIngredients.push_back(&availableIngredients[first]);
				selectedIngredients.push_back(&availableIngredients[second]);
				if (third != (std::numeric_limits<std::size_t>::max)()) {
					selectedIngredients.push_back(&availableIngredients[third]);
				}
				return effect::evaluatePotion(selectedIngredients, player, false, algorithm);
			};
			const auto evaluate = [&](std::size_t first, std::size_t second, std::size_t third) {
				return evaluateResult(first, second, third).valid;
			};
			const auto isBlueMountainFlowerWheat = [](const Ingredient& first, const Ingredient& second) {
				constexpr RE::FormID blueMountainFlower = 0x00077E1C;
				constexpr RE::FormID wheat = 0x0004B0BA;
				const auto firstFormID = first.nativeIngredient ? first.nativeIngredient->GetFormID() : 0;
				const auto secondFormID = second.nativeIngredient ? second.nativeIngredient->GetFormID() : 0;
				return (firstFormID == blueMountainFlower && secondFormID == wheat) ||
					(firstFormID == wheat && secondFormID == blueMountainFlower);
			};
			std::array<std::size_t, 3> blueMountainFlowerCanisJarrinIndices{
				(std::numeric_limits<std::size_t>::max)(),
				(std::numeric_limits<std::size_t>::max)(),
				(std::numeric_limits<std::size_t>::max)()
			};
			constexpr std::array<RE::FormID, 3> blueMountainFlowerCanisJarrinForms{
				0x00077E1C,
				0x0006ABCB,
				0x0001BCBC
			};
			for (std::size_t index = 0; index < availableIngredients.size(); ++index) {
				const auto formID = availableIngredients[index].nativeIngredient ?
					availableIngredients[index].nativeIngredient->GetFormID() : 0;
				for (std::size_t target = 0; target < blueMountainFlowerCanisJarrinForms.size(); ++target) {
					if (formID == blueMountainFlowerCanisJarrinForms[target]) {
						blueMountainFlowerCanisJarrinIndices[target] = index;
						break;
					}
				}
			}
			if (std::all_of(blueMountainFlowerCanisJarrinIndices.begin(), blueMountainFlowerCanisJarrinIndices.end(),
				[](const auto index) { return index != (std::numeric_limits<std::size_t>::max)(); })) {
				const auto targetResult = evaluateResult(
					blueMountainFlowerCanisJarrinIndices[0],
					blueMountainFlowerCanisJarrinIndices[1],
					blueMountainFlowerCanisJarrinIndices[2]);
				algorithmResult.blueMountainFlowerCanisJarrin = targetResult.valid;
				if (targetResult.valid) {
					algorithmResult.blueMountainFlowerCanisJarrinValue = targetResult.cost;
				}
			}

			const auto ingredientCount = availableIngredients.size();
			for (std::size_t first = 0; first + 1 < ingredientCount; ++first) {
				for (std::size_t second = first + 1; second < ingredientCount; ++second) {
					const bool candidate = shareEffect(first, second);
					const bool target = isBlueMountainFlowerWheat(availableIngredients[first], availableIngredients[second]);
					const bool valid = (candidate || target) &&
						evaluate(first, second, (std::numeric_limits<std::size_t>::max)());
					if (valid) {
						++algorithmResult.craftablePairs;
					}
					if (target) {
						algorithmResult.blueMountainFlowerWheat = valid;
					}
				}
			}
			for (std::size_t first = 0; first + 2 < ingredientCount; ++first) {
				for (std::size_t second = first + 1; second + 1 < ingredientCount; ++second) {
					for (std::size_t third = second + 1; third < ingredientCount; ++third) {
						if ((shareEffect(first, second) || shareEffect(first, third) || shareEffect(second, third)) &&
							evaluate(first, second, third)) {
							++algorithmResult.craftableTriples;
						}
					}
				}
			}
			result.algorithms.push_back(std::move(algorithmResult));
		}

		if (!result.algorithms.empty()) {
			result.totalsAgree = std::all_of(result.algorithms.begin() + 1, result.algorithms.end(),
				[&result](const auto& algorithm) {
					return algorithm.craftablePairs == result.algorithms.front().craftablePairs &&
						algorithm.craftableTriples == result.algorithms.front().craftableTriples;
				});
		}
		result.completed = true;
		return result;
	}

	std::optional<RecipeResult> FindRecipeForIngredients(const std::string& a_ingredients)
	{
		{
			std::lock_guard lock(snapshotMutex);
			for (const auto& recipe : cachedRecipes) {
				if (recipe.ingredients == a_ingredients) {
					return recipe;
				}
			}
		}
		{
			std::scoped_lock lock(masterCacheMutex);
			if (masterCache.isValid) {
				for (const auto& entry : masterCache.entries) {
					if (entry.result.ingredients == a_ingredients) {
						return entry.result;
					}
				}
			}
		}
		return std::nullopt;
	}

	std::optional<RecipeResult> FindRecipeForIngredientDetails(const std::string& a_ingredientDetails)
	{
		{
			std::lock_guard lock(snapshotMutex);
			for (const auto& recipe : cachedRecipes) {
				if (recipe.ingredientDetails == a_ingredientDetails) {
					return recipe;
				}
			}
		}
		{
			std::scoped_lock lock(masterCacheMutex);
			if (masterCache.isValid) {
				for (const auto& entry : masterCache.entries) {
					if (entry.result.ingredientDetails == a_ingredientDetails) {
						return entry.result;
					}
				}
			}
		}
		return std::nullopt;
	}

	std::optional<RecipeResult> FindRecipeForIngredientDetailsAtState(const std::string& a_ingredientDetails, const Player& a_player)
	{
		RecipeResult result;
		bool foundRecipe = false;
		{
			std::lock_guard lock(snapshotMutex);
			const auto found = std::find_if(cachedRecipes.begin(), cachedRecipes.end(), [&a_ingredientDetails](const auto& recipe) {
				return recipe.ingredientDetails == a_ingredientDetails;
			});
			if (found != cachedRecipes.end()) {
				result = *found;
				foundRecipe = true;
			}
		}
		if (!foundRecipe) {
			std::scoped_lock lock(masterCacheMutex);
			if (masterCache.isValid) {
				for (const auto& entry : masterCache.entries) {
					if (entry.result.ingredientDetails == a_ingredientDetails) {
						result = entry.result;
						foundRecipe = true;
						break;
					}
				}
			}
		}
		if (!foundRecipe) {
			return std::nullopt;
		}

		std::vector<std::uint32_t> formIDs;
		if (!ParseIngredientFormIDs(a_ingredientDetails, formIDs)) {
			return std::nullopt;
		}
		std::vector<Ingredient> selectedIngredients;
		selectedIngredients.reserve(formIDs.size());
		for (const auto formID : formIDs) {
			auto* form = RE::TESForm::LookupByID(formID);
			auto* ingredient = form ? form->As<IngredientItem>() : nullptr;
			if (!ingredient) {
				return std::nullopt;
			}
			selectedIngredients.emplace_back(ingredient);
		}

		std::vector<const Ingredient*> ingredientPointers;
		ingredientPointers.reserve(selectedIngredients.size());
		for (const auto& ingredient : selectedIngredients) {
			ingredientPointers.push_back(&ingredient);
		}
		const auto nativeResult = effect::evaluatePotion(ingredientPointers, a_player);
		if (!nativeResult.valid) {
			return std::nullopt;
		}

		std::vector<std::string> ingredientNames;
		ingredientNames.reserve(selectedIngredients.size());
		for (const auto& ingredient : selectedIngredients) {
			ingredientNames.push_back(ingredient.name);
		}
		std::sort(ingredientNames.begin(), ingredientNames.end());
		if (ingredientNames.size() == 2) {
			result.ingredients = str::printSort2(ingredientNames[0], ingredientNames[1]);
		} else {
			result.ingredients = str::printSort3(ingredientNames[0], ingredientNames[1], ingredientNames[2]);
		}
		result.ingredientDetails = FormatIngredientDetails(selectedIngredients);
		result.ingredientFormIDs = std::move(formIDs);
		result.effects = FormatEffects(nativeResult);
		result.calculationDetails = FormatCalculationDetails(nativeResult);
		result.calculatedValue = nativeResult.cost;
		result.displayedValue = static_cast<int>(std::floor(nativeResult.cost));
		return result;
	}

	void NotifyAlchemyMenuOpened()
	{
		std::scoped_lock lock(masterCacheMutex);
		masterCache.hasMenuClosedTime = false;
	}

	void NotifyAlchemyMenuClosed()
	{
		std::scoped_lock lock(masterCacheMutex);
		masterCache.menuClosedTime = std::chrono::steady_clock::now();
		masterCache.hasMenuClosedTime = true;
	}

	void InvalidateMasterCache()
	{
		{
			std::scoped_lock lock(masterCacheMutex);
			masterCache.isValid = false;
			masterCache.entries.clear();
			masterCache.availableFormIDs.clear();
			masterCache.hasMenuClosedTime = false;
		}
		{
			std::scoped_lock lock(staleMutex);
			isRecipeListStale.store(false, std::memory_order_release);
			staleReasonText.clear();
		}
		{
			std::scoped_lock lock(workerMutex);
			if (pendingJob) {
				pendingJob->cancelToken->store(true, std::memory_order_relaxed);
				pendingJob.reset();
			}
			if (currentActiveJob) {
				currentActiveJob->cancelToken->store(true, std::memory_order_relaxed);
			}
		}
	}

	bool IsRecipeListStale()
	{
		return isRecipeListStale.load(std::memory_order_acquire);
	}

	std::string GetStaleReason()
	{
		std::scoped_lock lock(staleMutex);
		return staleReasonText;
	}

	void RequestManualRecalculate()
	{
		{
			std::scoped_lock lock(staleMutex);
			isRecipeListStale.store(false, std::memory_order_release);
			staleReasonText.clear();
		}
		{
			std::scoped_lock lock(progressMutex);
			currentProgress.isUpdating = true;
			currentProgress.progressFraction = 0.05f;
			currentProgress.phase = "Starting recalculation...";
			currentProgress.current = 0;
			currentProgress.total = 0;
		}
		RecalculateAsync(nullptr, true);
	}

	std::uint32_t GetLastCalculationDurationMs()
	{
		return lastCalculationDurationMs.load(std::memory_order_acquire);
	}
}

