#pragma once

#include <algorithm>
#include <memory>

#include "ClangModel.h"
#include "ClapModel.h"
#include "CymbalModel.h"
#include "FmPercModel.h"
#include "KickModel.h"
#include "MembraneModel.h"
#include "MetalModel.h"
#include "RumbleModel.h"
#include "SnareModel.h"

namespace df
{

/** Everything the UI needs to know about each engine. Indexed by ModelId. */
inline const ModelInfo& getModelInfo (int modelIndex) noexcept
{
    static const ModelInfo info[kNumModels]
    {
        { "Kick", "Pitch-swept sine with a noise beater click",
          0xffe8833a, "Decay", "Click", "Punch", "Sweep",
          0.45f, 0.35f, 0.55f, 0.62f },

        { "Snare", "Three shell modes plus band-limited wires",
          0xffe05a5a, "Decay", "Tone", "Snap", "Bend",
          0.42f, 0.55f, 0.55f, 0.35f },

        { "Membrane", "Six modal partials - toms, congas, woodblocks",
          0xff4fbf7f, "Decay", "Bright", "Material", "Bend",
          0.40f, 0.45f, 0.55f, 0.30f },

        { "Metal", "Six-oscillator metallic bank - hats and shakers",
          0xff3fc4d4, "Decay", "Tone", "Metallic", "Bend",
          0.13f, 0.60f, 0.85f, 0.25f },

        { "Clap", "Staircase-gated noise bursts with a tail",
          0xffa97ae0, "Tail", "Tone", "Spread", "Bursts",
          0.40f, 0.45f, 0.45f, 0.55f },

        { "Cymbal", "Ring-modulated oscillator pairs - crash and ride",
          0xffe8c547, "Decay", "Tone", "Complexity", "Snap",
          0.60f, 0.60f, 0.55f, 0.60f },

        { "FM Perc", "Two-operator FM - blocks, cowbells, bells",
          0xff5b8de0, "Decay", "Tone", "Ratio", "Index",
          0.30f, 0.60f, 0.35f, 0.35f },

        { "Clang", "Inharmonic FM ring-modulated - atonal struck metal",
          0xffb0c4de, "Decay", "Tone", "Clang", "Bend",
          0.45f, 0.55f, 0.65f, 0.45f },

        { "Rumble", "Detuned sub drone with a filtered noise bed",
          0xff7a4fd0, "Decay", "Tone", "Grit", "Bend",
          0.55f, 0.35f, 0.50f, 0.55f }
    };

    return info[std::clamp (modelIndex, 0, kNumModels - 1)];
}

/** Creates a fresh instance of the requested engine. */
inline std::unique_ptr<DrumModel> createModel (int modelIndex)
{
    switch ((ModelId) std::clamp (modelIndex, 0, kNumModels - 1))
    {
        case ModelId::kick:     return std::make_unique<KickModel>();
        case ModelId::snare:    return std::make_unique<SnareModel>();
        case ModelId::membrane: return std::make_unique<MembraneModel>();
        case ModelId::metal:    return std::make_unique<MetalModel>();
        case ModelId::clap:     return std::make_unique<ClapModel>();
        case ModelId::cymbal:   return std::make_unique<CymbalModel>();
        case ModelId::fmPerc:   return std::make_unique<FmPercModel>();
        case ModelId::clang:    return std::make_unique<ClangModel>();
        case ModelId::rumble:   return std::make_unique<RumbleModel>();
        case ModelId::count:    break;
    }

    return std::make_unique<KickModel>();
}

} // namespace df
