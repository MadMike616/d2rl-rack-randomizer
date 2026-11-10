#include <D2RLPlugin/api.h>

#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "rack-randomizer",
    .name = "Rack Randomizer",
    .version = "1.0.0",
    .author = "MadMike",
    .description = "Randomizes weapon and armor rack base selection in offline games.",
    .flags = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

using Seed = std::uint64_t;
using Picker = int(__fastcall*)(const void* game, Seed* rng, int itemLevel, int type, int ignoreRarity);
using D2RL::Mutations::Result;

struct RackProfile {
    const char* selectorExport;
    std::uintptr_t dropRva;
    std::size_t dropSize;
    std::uint64_t dropHash;
    std::uintptr_t returnRva;
    std::uintptr_t thunkRva;
    std::array<std::uint8_t, 6> thunkBytes;
    std::uintptr_t slotRva;
    std::uint64_t selectorHash;
};

constexpr RackProfile Armor{
    "SelectArmorDropRow", 0x43F4F0, 499, 0x7E08CEA187BDF83B,
    0x43F584, 0x3E2B622, {0xFF, 0x25, 0x80, 0xF6, 0xFF, 0xFF},
    0x3E2ACA8, 0xE75E4B2022D3CFAB,
};
constexpr RackProfile Weapon{
    "SelectWeaponDropRow", 0x4414C0, 533, 0x1EB55855E83E8530,
    0x441566, 0x3E2B628, {0xFF, 0x25, 0x82, 0xF6, 0xFF, 0xFF},
    0x3E2ACB0, 0x1D687B2187677198,
};

// The server's player-count code reads this byte as the game mode.
constexpr std::uintptr_t GameModeOffset = 0x101;
// Observed in the offline server game on the audited native layout.
constexpr std::uint8_t OfflineGameMode = 3;
constexpr std::uintptr_t ModeReaderRva = 0x425202;
constexpr std::array<std::uint8_t, 24> ModeReader{
    0x0F, 0xB6, 0x8B, 0x01, 0x01, 0x00, 0x00, 0x8B, 0x54, 0x24, 0x30, 0x83,
    0xE9, 0x01, 0x74, 0x0A, 0x83, 0xE9, 0x01, 0x74, 0x05, 0x83, 0xF9, 0x01,
};

std::uintptr_t ExeBase{};
Picker OriginalArmor{};
Picker OriginalWeapon{};
std::atomic_bool Active{};
std::atomic_bool ReportedArmor{};
std::atomic_bool ReportedWeapon{};
const D2RL::PluginContext* Context{};

bool ReadBytes(std::uintptr_t address, void* output, std::size_t size) noexcept {
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             output, size, &copied) && copied == size;
}

template<std::size_t N>
bool Matches(std::uintptr_t address, const std::array<std::uint8_t, N>& expected) noexcept {
    std::array<std::uint8_t, N> actual{};
    return ReadBytes(address, actual.data(), N) && actual == expected;
}

bool MatchesBody(std::uintptr_t address, std::size_t size, std::uint64_t expected) noexcept {
    std::array<std::uint8_t, 533> code{};
    if (size > code.size() || !ReadBytes(address, code.data(), size)) return false;
    std::uint64_t hash = 0xCBF29CE484222325;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= code[i];
        hash *= 0x100000001B3;
    }
    return hash == expected;
}

Picker ValidateRack(HMODULE core, const RackProfile& rack) noexcept {
    const auto picker = reinterpret_cast<std::uintptr_t>(GetProcAddress(core, rack.selectorExport));
    std::uintptr_t installed{};
    if (!picker || !Matches(ExeBase + rack.thunkRva, rack.thunkBytes)
        || !MatchesBody(ExeBase + rack.dropRva, rack.dropSize, rack.dropHash)
        || !ReadBytes(ExeBase + rack.slotRva, &installed, sizeof(installed))
        || installed != picker || !MatchesBody(picker, 519, rack.selectorHash)) {
        return nullptr;
    }
    return reinterpret_cast<Picker>(picker);
}

int SelectBase(Picker original, const RackProfile& rack, std::atomic_bool& reported,
               const void* caller,
               const void* game, Seed* roomSeed, int level, int type, int ignoreRarity) noexcept {
    std::uint8_t mode{};
    Seed freshSeed{};
    if (Active.load(std::memory_order_acquire)
        && reinterpret_cast<std::uintptr_t>(caller) == ExeBase + rack.returnRva
        && game && roomSeed
        && ReadBytes(reinterpret_cast<std::uintptr_t>(game) + GameModeOffset, &mode, sizeof(mode))
        && mode == OfflineGameMode
        && BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&freshSeed),
                          sizeof(freshSeed), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
        && freshSeed != 0) {
        // The caller retains its original seed pointer for subsequent item creation.
        const int selected = original(game, &freshSeed, level, type, ignoreRarity);
        if (selected >= 0 && !reported.exchange(true, std::memory_order_relaxed)) {
            Context->LogInfo(&rack == &Armor
                ? "Armor stand selected a base using a private RNG."
                : "Weapon rack selected a base using a private RNG.");
        }
        return selected;
    }
    return original(game, roomSeed, level, type, ignoreRarity);
}

int __fastcall SelectArmor(const void* game, Seed* rng, int level, int type, int ignoreRarity) noexcept {
    return SelectBase(OriginalArmor, Armor, ReportedArmor, _ReturnAddress(),
                      game, rng, level, type, ignoreRarity);
}

int __fastcall SelectWeapon(const void* game, Seed* rng, int level, int type, int ignoreRarity) noexcept {
    return SelectBase(OriginalWeapon, Weapon, ReportedWeapon, _ReturnAddress(),
                      game, rng, level, type, ignoreRarity);
}

Result StagePicker(const D2RL::PluginContext* ctx, const D2RL::MutationService& mutations,
                   D2RL::Mutations::TransactionHandle transaction, const RackProfile& rack,
                   Picker original, Picker replacement) noexcept {
    const D2RL::Mutations::BytePatchRequest request{
        .structSize = D2RL::Mutations::BytePatchRequestSize,
        .rva = rack.slotRva,
        .expected = &original,
        .expectedSize = sizeof(original),
        .bytes = &replacement,
        .size = sizeof(replacement),
    };
    D2RL::Mutations::OperationHandle operation{};
    return mutations.stageBytePatch(ctx, transaction, &request, &operation);
}

} // namespace

extern "C" __declspec(dllexport) const D2RL::PluginInfo* D2RLoaderGetPluginInfo() noexcept {
    return &Info;
}

extern "C" __declspec(dllexport) bool D2RLoaderLoadPlugin(const D2RL::PluginContext* ctx) noexcept {
    if (!D2RL::HasContext(ctx) || !ctx->exeBase) return false;
    const D2RL::MutationService* mutations{};
    if (ctx->QueryService(&mutations) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)
        || !mutations->beginTransaction || !mutations->stageBytePatch
        || !mutations->commit || !mutations->cancelTransaction) {
        ctx->LogError("Rack Randomizer requires the D2RLoader mutation service.");
        return false;
    }

    ExeBase = ctx->exeBase;
    const HMODULE core = GetModuleHandleW(L"D2RCore.dll");
    const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
    // Both selectors call this candidate filter, which forwards the seed to the rarity roll.
    std::uintptr_t criteria{};
    if (!core || !Matches(ExeBase + ModeReaderRva, ModeReader)
        || !MatchesBody(ExeBase + 0x2EFBF0, 16, 0x9146A8FFCA39B92A)
        || !ReadBytes(coreBase + 0x6FFBF8, &criteria, sizeof(criteria))
        || criteria != ExeBase + 0x4445E0
        || !MatchesBody(criteria, 0x95, 0xB2C91D27C0BC07A3)
        || !MatchesBody(ExeBase + 0x153B00, 0x60, 0x81D7ABFA72A1C3D3)) {
        ctx->LogError("Rack Randomizer could not verify the native mode or RNG contract.");
        return false;
    }
    OriginalArmor = ValidateRack(core, Armor);
    OriginalWeapon = ValidateRack(core, Weapon);
    if (!OriginalArmor || !OriginalWeapon) {
        ctx->LogError("Rack Randomizer could not verify both pickers.");
        return false;
    }

    D2RL::Mutations::TransactionHandle transaction{};
    if (mutations->beginTransaction(ctx, &transaction) != Result::Success) return false;
    if (StagePicker(ctx, *mutations, transaction, Armor, OriginalArmor, SelectArmor) != Result::Success
        || StagePicker(ctx, *mutations, transaction, Weapon, OriginalWeapon, SelectWeapon) != Result::Success) {
        mutations->cancelTransaction(ctx, transaction);
        ctx->LogError("Rack Randomizer could not stage both rack picker slots.");
        return false;
    }

    D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
    if (mutations->commit(ctx, transaction, &result) != Result::Success || result.result != Result::Success) {
        ctx->LogError("Rack Randomizer could not commit both rack picker slots.");
        return false;
    }
    Context = ctx;
    Active.store(true, std::memory_order_release);
    ctx->LogInfo("Rack Randomizer armed for offline weapon racks and armor stands.");
    return true;
}

extern "C" __declspec(dllexport) void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false, std::memory_order_release);
}
