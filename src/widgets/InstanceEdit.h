#pragma once
#include <vector>
#include <dots/dots.h>

struct InstanceEdit
{
    InstanceEdit(dots::type::AnyStruct instance);
    InstanceEdit(const InstanceEdit& other) = delete;
    InstanceEdit(InstanceEdit&& other) = default;
    ~InstanceEdit() = default;

    InstanceEdit& operator = (const InstanceEdit& rhs) = delete;
    InstanceEdit& operator = (InstanceEdit&& rhs) = default;
    
    bool render();

private:

    bool m_firstRender;
    dots::type::AnyStruct m_instance;
    std::vector<dots::type::ProxyProperty<>> m_properties;
    std::vector<std::string> m_headers;
    std::vector<std::string> m_buffers;
    std::vector<std::string> m_labels;
    std::vector<std::optional<bool>> m_inputParsable;
};
