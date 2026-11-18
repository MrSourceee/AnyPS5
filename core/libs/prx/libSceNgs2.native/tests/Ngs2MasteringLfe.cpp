#include "Ngs2Test.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>

static float InputValue(std::uint32_t channel) {
    return channel == 3 ? 1.0f : (channel + 1) / 16.0f;
}

static int APS5_VABI Impulse(Ngs2UserFxProcessContext* context) {
    const bool pulse = *reinterpret_cast<const bool*>(context->user_data0);
    for (std::uint32_t channel = 0; channel < context->num_channels; ++channel) {
        std::fill_n(context->channel_data[channel], context->num_grain_samples, 0.0f);
        if (pulse) context->channel_data[channel][0] = InputValue(channel);
    }
    return SCE_NGS2_OK;
}

struct LfeRig {
    const std::size_t firstBuffer = usedBuffers;
    const std::uintptr_t system = CreateSystem();
    const std::uint32_t channels;
    const std::uint32_t grain;
    const std::uintptr_t master;
    bool pulse = false;

    LfeRig(std::uint32_t channels, std::uint32_t grain) : channels(channels), grain(grain), master(Mastering(system, channels)) {
        Require(sceNgs2SystemSetGrainSamples(system, grain) == SCE_NGS2_OK);
        const auto source = Voice(CreateRack(system, SCE_NGS2_RACK_ID_SUBMIXER));
        Control(source, SCE_NGS2_SUBMIXER_VOICE_PARAM_SETUP, Ngs2SubmixerVoiceSetupParam{{}, channels, 0});
        Control(source, SCE_NGS2_SUBMIXER_VOICE_PARAM_USER_FX, Ngs2SubmixerVoiceUserFxParam{{}, Impulse, reinterpret_cast<std::uintptr_t>(&pulse), 0, 0});
        Patch(source, master);
        Event(source, SCE_NGS2_VOICE_EVENT_PLAY);
    }

    ~LfeRig() {
        Require(sceNgs2SystemDestroy(system, nullptr) == SCE_NGS2_OK);
        ResetBuffers(firstBuffer);
    }

    void Set(std::uint32_t enabled, std::uint32_t cutoff) const {
        Control(master, SCE_NGS2_MASTERING_VOICE_PARAM_LFE, Ngs2MasteringVoiceEffectParam{{}, enabled, cutoff});
    }

    std::vector<float> Render(bool sendImpulse, std::uint32_t outputs = 0) {
        pulse = sendImpulse;
        if (outputs == 0) outputs = channels;
        std::vector<float> out(grain * outputs, -1.0f);
        const Ngs2RenderBufferInfo info{out.data(), out.size() * sizeof(float), SCE_NGS2_WAVEFORM_TYPE_PCM_F32L, outputs};
        Require(sceNgs2SystemRender(system, &info, 1) == SCE_NGS2_OK);
        return out;
    }
};

static std::vector<float> Response(std::uint32_t channels, std::uint32_t grain, std::uint32_t enabled, std::uint32_t cutoff) {
    LfeRig rig(channels, grain);
    rig.Set(enabled, cutoff);
    std::vector<float> response;
    for (std::uint32_t first = 0; first < 4096; first += grain) {
        const auto out = rig.Render(first == 0);
        for (std::uint32_t frame = 0; frame < grain; ++frame) {
            for (std::uint32_t c = 0; c < channels; ++c) {
                const float value = out[frame * channels + c];
                if (c == 3) response.push_back(value);
                else Require(std::bit_cast<std::uint32_t>(value) == std::bit_cast<std::uint32_t>(first + frame == 0 ? InputValue(c) : 0.0f));
            }
        }
    }
    return response;
}

static void CheckResponse(const std::vector<float>& response, const std::array<float, 4>& start, std::size_t frame, float height, float heightTolerance, double sum) {
    for (std::size_t i = 0; i < start.size(); ++i) Require(std::abs(response[i] - start[i]) < 0.005f * start[i]);
    const auto peak = std::max_element(response.begin(), response.end());
    Require(static_cast<std::size_t>(peak - response.begin()) == frame);
    Require(std::abs(*peak - height) < heightTolerance);
    Require(std::abs(std::accumulate(response.begin(), response.end(), 0.0) - sum) < 0.0015);
    Require(response.back() != 0.0f);
}

static void TestImpulses() {
    const auto low = Response(6, 256, 1, 80);
    const auto high = Response(6, 256, 1, 81);
    CheckResponse(low, {6.10e-10f, 4.87e-9f, 1.94e-8f, 5.32e-8f}, 276, 0.00452897f, 5e-6f, 0.988);
    CheckResponse(high, {3.07e-9f, 2.45e-8f, 9.75e-8f, 2.67e-7f}, 184, 0.00680154f, 1e-6f, 0.989);
    Require(Response(6, 256, 1, 120) == high);
    for (const auto channels : {4u, 6u, 8u}) {
        Require(Response(channels, 64, 2, 0) == low);
        Require(Response(channels, 512, 0xffffffffu, 1000000000) == high);
    }
}

static void TestChangesBetweenRenders() {
    LfeRig rig(6, 256);
    const auto dry = rig.Render(true);
    rig.Set(0, 120);
    Require(rig.Render(true) == dry);
    rig.Set(1, 80);
    const auto low = rig.Render(true);
    Require(low != dry);
    const auto tail = rig.Render(false);
    auto setup = [&] {
        Control(rig.master, SCE_NGS2_MASTERING_VOICE_PARAM_SETUP, Ngs2MasteringVoiceSetupParam{{}, rig.channels, 0});
        Event(rig.master, SCE_NGS2_VOICE_EVENT_PLAY);
    };
    setup();
    Require(rig.Render(true) == dry);
    rig.Set(1, 80);
    Require(rig.Render(true) == low);
    rig.Set(0, 81);
    Require(rig.Render(true) == dry);
    rig.Set(1, 80);
    Require(rig.Render(false) == tail);
    setup();
    rig.Set(1, 80);
    Require(rig.Render(true) == low);
    rig.Set(1, 81);
    const auto changed = rig.Render(false);
    Require(changed[3] != 0.0f && changed != tail);
    Event(rig.master, SCE_NGS2_VOICE_EVENT_STOP_IMM);
    Event(rig.master, SCE_NGS2_VOICE_EVENT_PLAY);
    bool refused = false;
    try { rig.Render(true); }
    catch (const std::runtime_error& error) { refused = std::string(error.what()).find("mastering LFE") != std::string::npos; }
    Require(refused);
    setup();
    Require(rig.Render(true) == dry);
}

static void TestUnmeasuredRoutes() {
    for (const auto [channels, outputs, rate] : {std::array{2u, 2u, 48000u}, std::array{6u, 2u, 48000u}, std::array{6u, 6u, 96000u}}) {
        LfeRig rig(channels, 256);
        Require(sceNgs2SystemSetSampleRate(rig.system, rate) == SCE_NGS2_OK);
        rig.Set(1, 80);
        bool refused = false;
        try { rig.Render(true, outputs); }
        catch (const std::runtime_error& error) { refused = std::string(error.what()).find("mastering LFE") != std::string::npos; }
        Require(refused);
        rig.Set(0, 80);
        rig.Render(true, outputs);
    }
}

int main() {
    TestImpulses();
    TestChangesBetweenRenders();
    TestUnmeasuredRoutes();
    return 0;
}
