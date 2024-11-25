#include <models/TransceiverModel.h>
#include <StructDescriptorData.dots.h>
#include <EnumDescriptorData.dots.h>
#include <fmt/format.h>
#include <dots_ext/FileOutChannel.h>

TransceiverModel::TransceiverModel() :
    m_publisherNameTexts{ std::make_shared<std::map<uint32_t, ImGuiExt::ColoredText>>() },
    m_publisherModel{ m_publisherNameTexts },
    m_eventIndex(0)
{
    m_subscriptions.emplace_back(dots::subscribe<StructDescriptorData>([](auto&){}));
    m_subscriptions.emplace_back(dots::subscribe<EnumDescriptorData>([](auto&){}));
    m_subscriptions.emplace_back(dots::subscribe<dots::type::StructDescriptor>({ &TransceiverModel::handleNewType, this }));
    m_subscriptions.emplace_back(dots::subscribe<DotsClient>({ &TransceiverModel::handleDotsClient, this }));
}

void TransceiverModel::subscribe(const StructDescriptorModel& descriptorModel, event_handler_t handler)
{
    m_eventHandlersTyped[&descriptorModel].emplace_back(std::move(handler));
}

void TransceiverModel::subscribe(event_handler_t handler)
{
    m_eventHandlers.emplace_back(std::move(handler));
}

void TransceiverModel::subscribe(struct_type_handler_t handler)
{
    m_structTypeHandlers.emplace_back(std::move(handler));
}

void TransceiverModel::writeTraceFile(std::filesystem::path path)
{
    dots::type::Registry registry{ std::nullopt, dots::type::Registry::StaticTypePolicy::InternalOnly };
    auto outChannel = dots::io::make_channel<dots::io::FileOutChannel>(dots::global_transceiver().ioContext(), std::move(path));
    outChannel->init(registry);

    for (const auto& [header, model] : m_eventData)
        outChannel->transmit(header, model.publishedInstanceModel().instance());
}

void TransceiverModel::handleNewType(const dots::type::StructDescriptor& descriptor)
{
    if (!descriptor.substructOnly())
    {
        const StructDescriptorModel& descriptorModel = m_descriptorModels.emplace_back(descriptor);

        for (auto& handler : m_structTypeHandlers)
        {
            handler(descriptorModel);
        }

        m_subscriptions.emplace_back(dots::subscribe(descriptor, [this, &descriptorModel](const dots::Event<>& event)
        {
            size_t eventIndex = ++m_eventIndex;
            size_t updateIndex;

            if (auto it = m_updateIndices.find(&event.updated()); it == m_updateIndices.end())
            {
                updateIndex = eventIndex;
                if (!event.isRemove())
                    m_updateIndices.emplace(&event.updated(), updateIndex);
            }
            else
            {
                updateIndex = it->second;
                if (event.isRemove())
                    m_updateIndices.erase(it);
            }

            const EventModel& eventModel = m_eventData.emplace_back(
                event.header(),
                EventModel{ eventIndex, updateIndex, MetadataModel{ m_publisherModel, event }, descriptorModel, event }
            ).second;

            for (auto& handler : m_eventHandlers)
            {
                handler(eventModel);
            }

            if (auto it = m_eventHandlersTyped.find(&descriptorModel); it != m_eventHandlersTyped.end())
            {
                for (auto& handler : it->second)
                {
                    handler(eventModel);
                }
            }
        }));
    }
}

void TransceiverModel::handleDotsClient(const dots::Event<DotsClient>& event)
{
    if (const auto& client = event(); client.name.isValid())
    {
        auto& publisherNameText = [this](dots::uint32_t id) -> auto&
        {
            if (auto it = m_publisherNameTexts->find(id); it == m_publisherNameTexts->end())
                return m_publisherNameTexts->try_emplace(id, fmt::format("\"<unknown> [{}]\"", id), ColorThemeActive.StringType).first->second;
            else
                return it->second;
        }(*client.id);

        publisherNameText.first = fmt::format("\"{}\"", *client.name);
    }
}
