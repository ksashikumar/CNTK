//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//

#include "stdafx.h"
#include "ImageReader.h"
#include "Config.h"
#include "ImageConfigHelper.h"
#include "ImageTransformers.h"
#include "BlockRandomizer.h"
#include "ImageDataDeserializer.h"
#include "FrameModePacker.h"
#include <omp.h>

namespace Microsoft { namespace MSR { namespace CNTK {

ImageReader::ImageReader(
    MemoryProviderPtr provider,
    const ConfigParameters& config)
    : m_provider(provider), m_seed(0)
{
    // In the future, deserializers and transformers will be dynamically loaded
    // from external libraries based on the configuration/brain script.
    // We will provide ability to implement the transformer and
    // deserializer interface not only in C++ but in scripting languages as well.

    ImageConfigHelper configHelper(config);
    m_streams = configHelper.GetStreams();
    assert(m_streams.size() == 2);

    int threadCount = configHelper.GetCpuThreadCount();
    if (threadCount > 0)
    {
        omp_set_num_threads(threadCount);
    }

    auto deserializer = std::make_shared<ImageDataDeserializer>(config);

    auto randomizer = std::make_shared<BlockRandomizer>(0, SIZE_MAX, deserializer);
    randomizer->Initialize(nullptr, config);

    auto cropper = std::make_shared<CropTransformer>();
    cropper->Initialize(randomizer, config);

    auto scaler = std::make_shared<ScaleTransformer>();
    scaler->Initialize(cropper, config);

    auto mean = std::make_shared<MeanTransformer>();
    mean->Initialize(scaler, config);

    TransformerPtr last = mean;
    if (configHelper.GetDataFormat() == CHW)
    {
        last = std::make_shared<TransposeTransformer>();
        last->Initialize(mean, config);
    }

    m_transformer = last;
}

std::vector<StreamDescriptionPtr> ImageReader::GetStreams()
{
    assert(!m_streams.empty());
    return m_streams;
}

void ImageReader::StartEpoch(const EpochConfiguration& config)
{
    assert(config.m_minibatchSizeInSamples > 0);
    assert(config.m_totalEpochSizeInSamples > 0);

    m_transformer->StartEpoch(config);
    m_packer = std::make_shared<FrameModePacker>(
        m_provider,
        m_transformer,
        config.m_minibatchSizeInSamples,
        m_streams);
}

Minibatch ImageReader::ReadMinibatch()
{
    assert(m_packer != nullptr);
    return m_packer->ReadMinibatch();
}
} } }
