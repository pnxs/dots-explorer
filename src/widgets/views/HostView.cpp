#include <widgets/views/HostView.h>
#include <boost/asio.hpp>
#include <imgui.h>
#include <fmt/format.h>
#include <dots/io/Io.h>
#include <dots/io/channels/LocalListener.h>
#include <dots_ext/FileInChannel.h>
#include <dots/tools/logging.h>
#include <common/Colors.h>
#include <common/Settings.h>
#include <common/System.h>
#include <DotsDescriptorRequest.dots.h>
#include <DotsCacheInfo.dots.h>

HostView::HostView(std::string appName) :
    m_state(State::Disconnected),
    m_deltaSinceError(0.0f),
    m_helpHintWidth(100.0f),
    m_hostSettings{ Settings::Register<HostSettings>() },
    m_viewSettings{ Settings::Register<ViewSettings>() },
    m_releaseInfoTask{ Version::GetReleaseInfo() },
    m_appName{ std::move(appName) }
{
    /* do nothing */
}

HostView::~HostView()
{
    disconnect();
}

void HostView::render()
{
    // update state
    update();

    bool openHostSettingsEdit = false;
    Host* editHost = nullptr;

    bool openOpenTraceDialog = false;
    bool openSaveTraceDialog = false;

    // render panel
    {
        dots::vector_t<Host>& hosts = *m_hostSettings.hosts;
        uint32_t& selectedHost = *m_hostSettings.selectedHost;

        // ensure hosts are valid
        {
            if (hosts.empty())
            {
                hosts.emplace_back(Host{ 
                    .endpoint = "tcp://127.0.0.1",
                    .description = "localhost (default)"
                });
            }

            if (selectedHost >= hosts.size() && selectedHost != NoHostSelected)
                selectedHost = 0;
        }

        // init endpoint buffer
        if (!m_hostEndpointEdit)
        {
            m_hostEndpointEdit.emplace(*m_hostSettings.activeHost);
        }

        // check dropped files
        if (!System::DroppedFiles.empty())
        {
            if (std::filesystem::path path = System::DroppedFiles.front(); is_regular_file(path))
            {
                *m_hostSettings.activeHost->endpoint = fmt::format("file:{}{}", path.root_path() == "/" ? "" : "/", path.string());
                *m_hostSettings.activeHost->description = path.filename().string();
                m_hostSettings.selectedHost = NoHostSelected;
                m_hostEndpointEdit.emplace(*m_hostSettings.activeHost);

                m_state = State::Pending;
            }

            System::DroppedFiles.clear();
        }

        // render host input
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Hosts ");

            ImGui::SameLine();
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.3f - 19);

            if (m_hostEndpointEdit->render())
            {
                selectedHost = NoHostSelected;
            }
            ImGui::PopItemWidth();
            ImGuiExt::TooltipLastHoveredItem(m_hostSettings.activeHost->endpoint->data());
        }

        ImGui::BeginDisabled(m_state == State::Connecting);

        // render hosts list
        {
            ImGui::SameLine(0, 0);

            if (ImGui::BeginCombo("##Hosts", "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_HeightLarge))
            {
                // render new host entry
                {
                    if (ImGui::Selectable("<New>"))
                        openHostSettingsEdit = true;
                }

                // render edit host entry
                {
                    ImGui::BeginDisabled(selectedHost == NoHostSelected);

                    if (ImGui::Selectable("<Edit>"))
                    {
                        openHostSettingsEdit = true;
                        editHost = &hosts[selectedHost];
                    }

                    ImGui::EndDisabled();

                }

                // render remove host entry
                {
                    ImGui::BeginDisabled(hosts.size() <= 1 || selectedHost == NoHostSelected);

                    if (ImGui::Selectable("<Remove>"))
                    {
                        hosts.erase(hosts.begin() + selectedHost);

                        if (selectedHost > hosts.size())
                            --selectedHost;
                        else
                            selectedHost = NoHostSelected;
                    }

                    ImGui::EndDisabled();
                }

                // render open trace entry
                {
                    if (ImGui::Selectable("<Open Trace>"))
                        openOpenTraceDialog = true;
                }

                // render save trace entry
                {
                    ImGui::BeginDisabled(!m_transceiverModel);

                    if (ImGui::Selectable("<Save Trace>"))
                        openSaveTraceDialog = true;

                    ImGui::EndDisabled();
                }

                ImGui::Separator();

                // render stored connections
                {
                    ImGui::TextUnformatted("Connections:");
                    uint32_t i = 0;

                    for (Host& host : hosts)
                    {
                        if (ImGui::Selectable(host.description->data(), selectedHost == i) && selectedHost != i)
                        {
                            selectedHost = i;
                            m_hostSettings.activeHost = hosts[selectedHost];
                            m_hostEndpointEdit.emplace(*m_hostSettings.activeHost);
                            m_state = State::Pending;
                        }

                        ImGuiExt::TooltipLastHoveredItem(*host.endpoint);

                        ++i;
                    }

                    ImGui::EndCombo();
                }
            }
        }

        // render auto connect option
        {
            ImGui::SameLine();
            ImGui::Checkbox("Auto", &*m_hostSettings.autoConnect);

            if (*m_hostSettings.autoConnect && (m_state == State::Disconnected || (m_state == State::Error && m_deltaSinceError > 5.0f)))
                m_state = State::Pending;

            ImGuiExt::TooltipLastHoveredItem("Automatically connect/reconnect on startup and connection loss.");
        }

        // render connect button
        {
            ImGui::SameLine();

            if (m_state == State::Connected)
            {
                if (ImGui::Button("Disconnect"))
                {
                    *m_hostSettings.autoConnect = false;
                    disconnect();
                }
            }
            else
            {
                if (ImGui::Button("Connect"))
                    m_state = State::Pending;
            }
        }

        ImGui::EndDisabled();

        // render connection state
        {
            constexpr std::pair<const char*, ImVec4> StateStrs[] =
            {
                { "[disconnected]", ColorThemeActive.Disabled },
                { "[pending]     ", ColorThemeActive.Pending },
                { "[connecting]  ", ColorThemeActive.Pending },
                { "[connected]   ", ColorThemeActive.Success },
                { "[error]       ", ColorThemeActive.Error }
            };

            auto [stateStr, stateColor] = StateStrs[static_cast<uint8_t>(m_state)];
            ImGui::SameLine();
            ImGui::TextColored(stateColor, "%s", stateStr);
        }

        View& selectedView = *m_viewSettings.selectedView;

        // process view select key
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            if (selectedView == View::Cache)
                selectedView = View::Trace;
            else if (selectedView == View::Trace)
                selectedView = View::Cache;
        }

        // render view selector
        if (m_cacheView != std::nullopt)
        {
            constexpr std::pair<View, const char*> ViewLabels[] = {
                { View::Cache, "Cache" },
                { View::Trace, "Trace" }
            };

            ImGui::SameLine();
            ImGui::TextUnformatted("View");

            ImGui::SameLine();
            ImGui::PushItemWidth(120.0f);

            if (ImGui::BeginCombo("##Views", ViewLabels[static_cast<int>(selectedView)].second))
            {
                for (const auto& [view, label] : ViewLabels)
                {
                    if (ImGui::Selectable(label, view == selectedView) && view != selectedView)
                        selectedView = view;
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();
        }

        // render help area
        {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - m_helpHintWidth);

            if (m_releaseInfoTask != std::nullopt)
            {
                if (auto status = m_releaseInfoTask->wait_for(std::chrono::milliseconds{ 0 }); status == std::future_status::ready)
                {
                    try
                    {
                        m_releaseInfo.emplace(m_releaseInfoTask->get());
                    }
                    catch (...)
                    {
                    }

                    m_releaseInfoTask = std::nullopt;
                }
            }

            const char* label;
            int colorsPushed = 1;

            if (m_releaseInfo != std::nullopt && m_releaseInfo->tag_name.isValid() && *m_releaseInfo->tag_name > Version::CurrentVersion)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                label = "Update Available [F1]";
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ColorThemeActive.Disabled);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_Button]);
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
                label = "Help/About [F1]";
                colorsPushed = 3;
            }

            if (ImGui::Button(label) || ImGui::IsKeyPressed(ImGuiKey_F1, false))
                m_helpDialog.emplace(m_releaseInfo == std::nullopt ? nullptr : &*m_releaseInfo);

            ImGui::PopStyleColor(colorsPushed);
            m_helpHintWidth = ImGui::GetItemRectSize().x;

            if (m_helpDialog != std::nullopt && !m_helpDialog->render())
                m_helpDialog = std::nullopt;
        }

        ImGui::Separator();

        // render views
        if (m_cacheView != std::nullopt)
        {
            if (selectedView == View::Cache)
                m_cacheView->render();
            else if (selectedView == View::Trace)
                m_traceView->render();
        }
    }

    // render host settings edit
    {
        if (openHostSettingsEdit)
            m_hostSettingsEdit.emplace(m_hostSettings, *m_hostSettings.activeHost, editHost);

        if (m_hostSettingsEdit != std::nullopt && !m_hostSettingsEdit->render())
            m_hostSettingsEdit = std::nullopt;
    }

    // render open trace file dialog
    {
        if (openOpenTraceDialog)
            m_fileOpenDialog.emplace();

        if (m_fileOpenDialog != std::nullopt && !m_fileOpenDialog->render())
        {
            if (m_fileOpenDialog->file())
            {
                std::filesystem::path path = *m_fileOpenDialog->file();
                *m_hostSettings.activeHost->endpoint = fmt::format("file:{}{}", path.root_path() == "/" ? "" : "/", path.string());
                *m_hostSettings.activeHost->description = path.filename().string();
                m_hostSettings.selectedHost = NoHostSelected;
                m_hostEndpointEdit.emplace(*m_hostSettings.activeHost);

                m_state = State::Pending;
            }

            m_fileOpenDialog = std::nullopt;
        }
    }

    // render save trace file dialog
    {
        if (openSaveTraceDialog)
            m_fileSaveDialog.emplace();

        if (m_fileSaveDialog != std::nullopt && !m_fileSaveDialog->render())
        {
            if (m_fileSaveDialog->file())
                m_transceiverModel->writeTraceFile(*m_fileSaveDialog->file());

            m_fileSaveDialog = std::nullopt;
        }
    }
}

void HostView::disconnect()
{
    boost::asio::io_context& ioContext = dots::io::global_io_context();

    try
    {
        ioContext.stop();
        m_cacheView.reset();
        m_traceView.reset();
        m_transceiverModel.reset();
        dots::global_transceiver_destroy();
        ioContext.restart();
        ioContext.poll();
    }
    catch (...)
    {
    }

    ioContext.restart();
    m_state = State::Disconnected;
}

void HostView::update()
{
    switch (m_state)
    {
        case State::Disconnected:
            break;
        case State::Pending:
            disconnect();
            m_connectTask = std::async(std::launch::async, [this]
            {
                using transition_handler_t = dots::GuestTransceiver::transition_handler_t;
                dots::GuestTransceiver& transceiver = dots::global_transceiver_create(dots::GuestTransceiver(
                    m_appName,
                    dots::io::global_io_context(),
                    dots::type::Registry::StaticTypePolicy::InternalOnly,
                    transition_handler_t{ &HostView::handleTransceiverTransition, this }
                ));

                if (dots::global_transceiver().registry().findType("DotsCacheInfo") == nullptr) {
                    dots::global_transceiver().registry().registerType(dots::type::Descriptor<DotsCacheInfo>::Instance());
                }

                dots::io::Endpoint endpoint{ *m_hostSettings.activeHost->endpoint };

                if (endpoint.scheme() == "file" || endpoint.scheme() == "file-v1")
                {
                    std::string_view path = endpoint.path();
                    #ifdef _WIN32
                    if (path.front() == '/')
                        path.remove_prefix(1);
                    #endif
                    transceiver.open<dots::io::FileInChannel>(path);
                }
                else
                    transceiver.open(endpoint);

                for (;;)
                {
                    if (m_connectionError != nullptr)
                        std::rethrow_exception(m_connectionError);

                    if (transceiver.connected())
                        break;
                    else
                        dots::global_transceiver().ioContext().run_one();
                }
            });
            m_state = State::Connecting;
            break;
        case State::Connecting:
            try
            {
                if (auto status = m_connectTask->wait_for(std::chrono::milliseconds{ 5 }); status == std::future_status::ready)
                {
                    m_connectTask->get();
                    m_transceiverModel.emplace();
                    m_cacheView.emplace(*m_transceiverModel);
                    m_traceView.emplace(*m_transceiverModel);
                    dots::publish(DotsDescriptorRequest{});
                    m_state = State::Connected;
                }
            }
            catch (...)
            {
                m_deltaSinceError = 0.0f;
                m_state = State::Error;
            }
            break;
        case State::Connected:
            {
                using namespace dots::literals;
                auto start = dots::steady_timepoint_t::Now();

                for (size_t totalHandlersExecuted = 0;;)
                {
                    size_t handlersExecuted = dots::global_transceiver().ioContext().poll_one();
                    totalHandlersExecuted += handlersExecuted;

                    if (!handlersExecuted || (totalHandlersExecuted % 1000 == 0 && (dots::steady_timepoint_t::Now() - start > 15ms)))
                        break;
                }
            }
            break;
        case State::Error:
            m_deltaSinceError += ImGui::GetIO().DeltaTime;
            break;
    }
}

void HostView::handleTransceiverTransition(const dots::Connection& connection, std::exception_ptr ePtr)
{
    m_connectionError = ePtr;

    if (connection.closed())
    {
        m_deltaSinceError = 0.0f;
        m_state = State::Error;
    }
}
