#include "Gmm.h"
#include "pugixml.hpp"
#include <regex>
#include <string>

static const char WINCTXNAME[] = "window_context_name";
static const char LANGUAGE[] = "English";
static const char JAPANESE[] = "日本語";

// Reads header constants from the supplied file.
// Expects them to be of the format `#define +{name} +{integer value}`
// and that the integer value is in sequential order starting from 0.
void GMM::ReadGmmHeader(const string &_filename) {
    ifstream hstrm(_filename, ios::ate | ios::binary);
    if (!hstrm.good()) {
        return;
    }
    int fsize = hstrm.tellg();
    auto buf = new char[fsize + 1];
    hstrm.seekg(0);
    hstrm.read(buf, fsize);
    buf[fsize] = 0;
    string hstrng(buf);
    regex pattern(R"(#define\s+(\w+)\s+([0-9]+))");
    smatch results;
    id_strings.clear();
    while (regex_search(hstrng, results, pattern)) {
        id_strings.emplace_back(results[1]);
        hstrng = results.suffix().str();
    }
    delete[] buf;
}

// Reads header constants to the supplied file.
// Prints them in the format `#define {name} {integer value}`
// such that the integer value is in sequential order starting from 0.
void GMM::WriteGmmHeader(const string &_filename) {
    ofstream hstrm(_filename);
    string guard(_filename);
    for (auto & c : guard) {
        switch (c) {
        case '/':
        case '.':
            c = '_';
            break;
        default:
            c = toupper(c);
            break;
        }
    }

    hstrm << "/***************************************************" << endl;
    hstrm << " * WARNING: This file was autogenerated by msgenc. *" << endl;
    hstrm << " *               DO NOT MODIFY                     *" << endl;
    hstrm << " ***************************************************/" << endl;
    hstrm << endl;
    hstrm << "#ifndef MSGENC_" << guard << endl;
    hstrm << "#define MSGENC_" << guard << endl;
    hstrm << endl;
    for (size_t i = 0; i < id_strings.size(); i++) {
        auto message_lines = SplitMessage(messages[i]);
        for (const auto & message_line : message_lines) {
            hstrm << "// " << message_line << endl;
        }
        hstrm << "#define " << id_strings[i] << " " << i << endl;
    }
    hstrm << endl;
    hstrm << "#endif //MSGENC_" << guard << endl;
}

// Reads messages from GMM into memory to be converted
int GMM::FromFile(MessagesConverter &converter) {
    pugi::xml_parse_result result = doc.load(stream);
    if (!result) {
        throw runtime_error(result.description());
    }
    const auto &node = doc.find_child([](const auto &n) {
        return strcmp(n.name(), "body") == 0;
    });
    int i = 0;
    int key = GMM_KEY_NOT_DEFINED;
    string rowname_pref = filename.substr(filename.find_last_of('/') + 1).substr(0, filename.find_first_of('.'));
    for (const auto &subnode : node.children()) {
        if (strcmp(subnode.name(), "row") == 0) {
            const auto &language = subnode.find_child([](const auto &n) {
                return strcmp(n.name(), "language") == 0 && strcmp(n.attribute("name").value(), LANGUAGE) == 0;
            });
            const auto &windowcontext = subnode.find_child([](const auto &n) {
                return strcmp(n.name(), "attribute") == 0 && strcmp(n.attribute("name").value(), WINCTXNAME) == 0;
            });
            string is_garbage(windowcontext.child_value());
            string message(language.child_value());
            if (is_garbage == "garbage") {
                const auto &jap = subnode.find_child([](const auto &n) {
                    return strcmp(n.name(), "language") == 0 && strcmp(n.attribute("name").value(), JAPANESE) == 0;
                });
                message.assign(jap.child_value());
                fill(message.begin(), message.end(), ' ');
            }
            converter.GetDecodedMessages().emplace_back(message);
            string row_id(subnode.attribute("id").value());
            if (row_id.empty()) {
                row_id = rowname_pref + '_' + row_no_buf;
            }
            id_strings.emplace_back(row_id);
            messages.emplace_back(message);
            i++;
            IncRowNoBuf();
        }
        if (strcmp(subnode.name(), "key") == 0) {
            key = stoi(subnode.attribute("value").value(), nullptr, 0);
            key &= 0xFFFF;
            key |= 0x10000;
        }
    }
    if (!converter.GetHeaderFilename().empty()) {
        WriteGmmHeader(converter.GetHeaderFilename());
    }
    return key;
}

// Writes decoded messages to a new GMM file
void GMM::ToFile(MessagesConverter &converter) {
    if (!converter.GetHeaderFilename().empty()) {
        ReadGmmHeader(converter.GetHeaderFilename());
    }
    auto it = id_strings.cbegin();
    auto body = doc.append_child("body");
    body.append_attribute("language").set_value(LANGUAGE);
    if (body.empty()) {
        throw runtime_error("failed to create gmm body node");
    }
    string rowname_pref = filename.substr(filename.find_last_of('/') + 1);
    rowname_pref = rowname_pref.substr(0, rowname_pref.find_first_of('.'));

    auto key = body.append_child("key");
    key.append_attribute("value").set_value(std::to_string(converter.GetKey()).c_str());

    int i = 0;
    for (const auto &message : converter.GetDecodedMessages()) {
        string rowname;
        if (it != id_strings.cend()) {
            rowname = *it++;
        } else {
            rowname = rowname_pref + '_' + row_no_buf;
        }
        auto row = body.append_child("row");
        if (row.empty()) {
            throw runtime_error("failed to create gmm row node");
        }
        row.append_attribute("id").set_value(rowname.c_str());
        row.append_attribute("index").set_value(i++);
        auto windowcontextname = row.append_child("attribute");
        windowcontextname.append_attribute("name").set_value(WINCTXNAME);
        auto language = row.append_child("language");
        if (language.empty()) {
            throw runtime_error("failed to create gmm language node");
        }
        language.append_attribute("name").set_value(LANGUAGE);
        auto windowattr = windowcontextname.append_child(pugi::xml_node_type::node_pcdata);
        if (message.find_first_not_of(' ') == string::npos) {
            string buf;
            buf.resize(message.size());
            fill(buf.begin(), buf.end(), 'X');
            windowattr.set_value("garbage");
            auto jap = row.append_child("language");
            jap.append_attribute("name").set_value(JAPANESE);
            jap.append_child(pugi::xml_node_type::node_pcdata).set_value(buf.c_str());
            language.append_child(pugi::xml_node_type::node_pcdata).set_value("");
        } else {
            windowattr.set_value("used");
            language.append_child(pugi::xml_node_type::node_pcdata).set_value(message.c_str());
        }
        IncRowNoBuf();
    }
    doc.save(stream);
}

// The message headers are included in script files, which can cause the
// assembler to break if the lines are too long.
vector<string> GMM::SplitMessage(const string &message) {
    vector<string> v;

    auto limit = 300;
    for (size_t i = 0; i < message.size(); i += limit) {
        v.push_back(message.substr(i, limit));
    }
    return v;
}
