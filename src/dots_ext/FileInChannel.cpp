#include <dots_ext/FileInChannel.h>
#include <dots/io/DescriptorConverter.h>
#include <fstream>
#include <dots/asio.h>
#include <dots/HostTransceiver.h>
#include <dots/tools/logging.h>

namespace dots::io::details
{
    GenericFileInChannel::GenericFileInChannel(key_t key, asio::io_context& ioContext, std::filesystem::path path) :
        LocalChannel(key, ioContext),
        m_fileRegistry{ std::nullopt, type::Registry::StaticTypePolicy::InternalOnly },
        m_transmissionsRead(0),
        m_ioContext{ ioContext },
        m_path{ std::move(path) }
    {
        loadFile();
    }

    void GenericFileInChannel::asyncReceiveImpl()
    {
        if (m_fileConnection == std::nullopt)
        {
            auto fileChannel = make_channel<LocalChannel>(m_ioContext);
            link(*fileChannel);
            fileChannel->link(*this);
            m_fileConnection.emplace(std::move(fileChannel), true);
            m_fileConnection->asyncReceive(m_fileRegistry, nullptr, "dots-file-host",
                { &GenericFileInChannel::handleTransmission, this },
                { &GenericFileInChannel::handleTransition, this }
            );
        }
        else if (m_fileConnection->connected())
        {
            if (m_serializer.inputAvailable())
            {
                if (m_transmissionsRead++ % 10000 == 0)
                    asio::post(m_ioContext.get(), [this]{ asyncReceiveImpl(); });
                else
                {
                    try
                    {
                        DotsHeader header = m_serializer.deserialize<DotsHeader>();

                        //TODO: make feature-switch configurable
                        bool skip_missing = true;

                        auto* td = m_fileRegistry.findStructType(*header.typeName, !skip_missing);
                        if (td == nullptr)
                        {
                            LOG_WARN_S("Skip missing type: " << *header.typeName);
                            m_serializer.reader().skip();
                            asio::post(m_ioContext.get(), [this] { asyncReceiveImpl(); });
                            return;
                        }

                        type::AnyStruct instance{ *td };
                        m_serializer.deserialize(*instance);

                        try {
                            if (auto *structDescriptorData = instance->_as<StructDescriptorData>())
                                DescriptorConverter{m_fileRegistry}(*structDescriptorData);
                            else if (auto *enumDescriptorData = instance->_as<EnumDescriptorData>())
                                DescriptorConverter{m_fileRegistry}(*enumDescriptorData);
                        } catch(const std::exception& e)
                        {
                            if (skip_missing) {
                                LOG_WARN_S("Skip missing type: " << *header.typeName);
                                m_serializer.reader().skip();
                                asio::post(m_ioContext.get(), [this] { asyncReceiveImpl(); });
                                return;
                            }
                            else {
                                throw;
                            }
                        }

                        header.attributes.valueOrEmplace(instance->_validProperties());
                        m_fileConnection->transmit(header, instance);

                    }
                    catch (...)
                    {
                        m_serializer.setInput(m_serializer.inputData(), 0);
                        processError(std::current_exception());
                    }
                }
            }
        }
    }

    bool GenericFileInChannel::handleTransmission(Connection&/* connection*/, Transmission/* transmission*/)
    {
        return true;
    }

    void GenericFileInChannel::handleTransition(Connection&/* connection*/, std::exception_ptr/* ePtr*/) noexcept
    {
        /* do nothing */
    }

    void GenericFileInChannel::loadFile()
    {
        std::ifstream file;
        file.exceptions(std::iostream::failbit | std::iostream::badbit);
        file.open(m_path.string(), std::ios::binary);

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        m_buffer.resize(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(m_buffer.data()), static_cast<std::streamsize>(size));

        m_serializer.setInput(m_buffer.data(), m_buffer.size());
    }

}
