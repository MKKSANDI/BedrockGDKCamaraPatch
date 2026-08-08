#include "installer/manifest.hpp"

#include <array>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace mcfix::installer {
namespace {

bool has_invalid_xml_control(std::string_view value) noexcept {
    for (const auto byte : value) {
        const auto code = static_cast<unsigned char>(byte);
        if (code < 0x20 && code != '\t' && code != '\n' && code != '\r') {
            return true;
        }
    }
    return false;
}

std::string xml_escape(std::string_view value) {
    if (has_invalid_xml_control(value)) {
        throw std::invalid_argument("manifest value contains an XML control character");
    }

    std::string escaped;
    escaped.reserve(value.size());
    for (const auto character : value) {
        switch (character) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '\"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

bool valid_version(std::string_view version) noexcept {
    std::array<unsigned int, 4> parts{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto separator = version.find('.', start);
        const auto end = index + 1 == parts.size() ? version.size() : separator;
        if (end == std::string_view::npos || end == start ||
            (index + 1 == parts.size() && separator != std::string_view::npos)) {
            return false;
        }
        const auto* first = version.data() + start;
        const auto* last = version.data() + end;
        const auto result = std::from_chars(first, last, parts[index]);
        if (result.ec != std::errc{} || result.ptr != last || parts[index] > 65535U) {
            return false;
        }
        start = end + 1;
    }
    return start == version.size() + 1;
}

void validate(const ManifestInput& input) {
    if (input.identity_name.empty() || input.publisher.empty() ||
        input.main_package_name.empty() || input.main_package_publisher.empty()) {
        throw std::invalid_argument("manifest identities and publishers must be non-empty");
    }
    if (input.architecture != "x64") {
        throw std::invalid_argument("only x64 modification packages are supported");
    }
    if (!valid_version(input.version)) {
        throw std::invalid_argument("package version must contain four 0-65535 components");
    }
}

}  // namespace

std::string render_modification_manifest(const ManifestInput& input) {
    validate(input);
    const auto identity_name = xml_escape(input.identity_name);
    const auto publisher = xml_escape(input.publisher);
    const auto version = xml_escape(input.version);
    const auto main_package_name = xml_escape(input.main_package_name);
    const auto main_package_publisher = xml_escape(input.main_package_publisher);
    const auto architecture = xml_escape(input.architecture);

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\" "
        << "xmlns:uap4=\"http://schemas.microsoft.com/appx/manifest/uap/windows10/4\" "
        << "IgnorableNamespaces=\"uap4\">\n"
        << "  <Identity Name=\"" << identity_name << "\" Publisher=\""
        << publisher << "\" Version=\"" << version
        << "\" ProcessorArchitecture=\"" << architecture << "\" />\n"
        << "  <Properties>\n"
        << "    <DisplayName>MCFIX Camera Patch</DisplayName>\n"
        << "    <PublisherDisplayName>MCFIX</PublisherDisplayName>\n"
        << "    <Logo>Assets\\StoreLogo.png</Logo>\n"
        << "  </Properties>\n"
        << "  <Dependencies>\n"
        << "    <TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.18362.0\" "
        << "MaxVersionTested=\"10.0.26200.0\" />\n"
        << "    <uap4:MainPackageDependency Name=\"" << main_package_name
        << "\" Publisher=\"" << main_package_publisher << "\" />\n"
        << "  </Dependencies>\n"
        << "</Package>\n";
    return xml.str();
}

}  // namespace mcfix::installer
