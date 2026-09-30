#include <iostream>
#include "core/pack.h"

std::vector<char> load_file(std::string path, bool& is_readen) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        is_readen = false;
        return {};
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (file.read(buffer.data(), size)) { is_readen = true; }
    else { is_readen = false; }
    file.close();
    return buffer;
}

bool create_file(const char* path, std::vector<char> buffer) {
    std::ofstream out_file(path, std::ios::out | std::ios::binary);
    if (!out_file) {
        return false;
    }

    out_file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    out_file.close();
    return true;
}

int main()
{
    struct staged_file {
        std::string source_path;
        std::string stored_path;
        std::vector<char> data;
    };

    std::vector<staged_file> staged_files;

    auto print_help = []() {
        std::cout
            << "Commands:\n"
            << "  add <source> [stored-name]       add a file to the pack queue\n"
            << "  remove <stored-name>             remove a file from the queue\n"
            << "  list                             show queued files\n"
            << "  clear                            clear the queue\n"
            << "  pack [bundle-name]               pack queued files (.x is automatic)\n"
            << "  unpack <bundle> <name> [output]  extract one file\n"
            << "  help                             show this help\n"
            << "  exit                             close the program\n"
            << "Paths containing spaces must be placed in quotes.\n";
        };

    auto parse_command = [](const std::string& line, std::vector<std::string>& arguments) {
        std::string argument;
        bool is_quoted = false;
        bool argument_started = false;

        for (char character : line) {
            if (character == '"') {
                is_quoted = !is_quoted;
                argument_started = true;
                continue;
            }

            bool is_separator = character == ' ' || character == '\t';
            if (is_separator && !is_quoted) {
                if (argument_started) {
                    arguments.push_back(argument);
                    argument.clear();
                    argument_started = false;
                }
                continue;
            }

            argument.push_back(character);
            argument_started = true;
        }

        if (is_quoted) {
            return false;
        }

        if (argument_started) {
            arguments.push_back(argument);
        }

        return true;
        };

    std::cout << "format bundle REPL\n";
    print_help();

    std::string line;
    while (true) {
        std::cout << "\nformat> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }

        std::vector<std::string> arguments;
        if (!parse_command(line, arguments)) {
            std::cerr << "unclosed quote\n";
            continue;
        }
        if (arguments.empty()) {
            continue;
        }

        std::string command = arguments[0];
        for (char& character : command) {
            if (character >= 'A' && character <= 'Z') {
                character += 'a' - 'A';
            }
        }

        try {
            if (command == "exit" || command == "quit") {
                break;
            }

            if (command == "help") {
                print_help();
                continue;
            }

            if (command == "list") {
                if (staged_files.empty()) {
                    std::cout << "pack queue is empty\n";
                    continue;
                }

                for (u64 file_id = 0; file_id < staged_files.size(); file_id++) {
                    const staged_file& file = staged_files[file_id];
                    std::cout << file_id << ": " << file.source_path << " -> "
                        << file.stored_path << " (" << file.data.size() << " bytes)\n";
                }
                continue;
            }

            if (command == "clear") {
                staged_files.clear();
                std::cout << "pack queue cleared\n";
                continue;
            }

            if (command == "add") {
                if (arguments.size() < 2 || arguments.size() > 3) {
                    std::cerr << "usage: add <source> [stored-name]\n";
                    continue;
                }
                const std::string& source_path = arguments[1];
                std::string stored_path;
                if (arguments.size() == 3) {
                    stored_path = arguments[2];
                }
                else {
                    size_t file_name_start = source_path.find_last_of("/\\");
                    stored_path = file_name_start == std::string::npos
                        ? source_path
                        : source_path.substr(file_name_start + 1);
                }

                if (stored_path.empty()) {
                    std::cerr << "stored name cannot be empty\n";
                    continue;
                }

                u64 stored_hash = fnv1a_hash_64(stored_path.data(), stored_path.size());
                bool duplicate_found = false;
                for (const staged_file& file : staged_files) {
                    u64 file_hash = fnv1a_hash_64(file.stored_path.data(), file.stored_path.size());
                    if (file.stored_path == stored_path) {
                        std::cerr << "stored name already exists: " << stored_path << "\n";
                        duplicate_found = true;
                        break;
                    }
                    if (file_hash == stored_hash) {
                        std::cerr << "hash collision with stored name: " << file.stored_path << "\n";
                        duplicate_found = true;
                        break;
                    }
                }
                if (duplicate_found) {
                    continue;
                }

                bool is_readen = false;
                std::vector<char> buffer = load_file(source_path, is_readen);
                if (!is_readen) {
                    std::cerr << "failed to load file: " << source_path << "\n";
                    continue;
                }
                if (buffer.empty()) {
                    std::cerr << "empty files are not supported\n";
                    continue;
                }

                staged_files.push_back({ source_path, stored_path, {} });
                staged_files.back().data.swap(buffer);
                std::cout << "added " << stored_path << "\n";
                continue;
            }

            if (command == "remove") {
                if (arguments.size() != 2) {
                    std::cerr << "usage: remove <stored-name>\n";
                    continue;
                }

                bool file_removed = false;
                for (auto file = staged_files.begin(); file != staged_files.end(); file++) {
                    if (file->stored_path == arguments[1]) {
                        staged_files.erase(file);
                        file_removed = true;
                        break;
                    }
                }

                if (!file_removed) {
                    std::cerr << "file is not in the pack queue: " << arguments[1] << "\n";
                    continue;
                }

                std::cout << "removed " << arguments[1] << "\n";
                continue;
            }

            if (command == "pack") {
                if (arguments.size() > 2) {
                    std::cerr << "usage: pack [bundle-name]\n";
                    continue;
                }
                if (staged_files.empty()) {
                    std::cerr << "pack queue is empty\n";
                    continue;
                }

                std::string bundle_name = arguments.size() == 2 ? arguments[1] : "bundle_000";
                if (bundle_name.size() >= 2 && bundle_name.substr(bundle_name.size() - 2) == ".x") {
                    bundle_name.erase(bundle_name.size() - 2);
                }
                if (bundle_name.empty()) {
                    std::cerr << "bundle name cannot be empty\n";
                    continue;
                }

                packer main_packer;
                bool files_added = true;
                for (staged_file& file : staged_files) {
                    if (!main_packer.add_file(file.stored_path,
                        reinterpret_cast<u8*>(file.data.data()), file.data.size())) {
                        std::cerr << "failed to add file: " << file.stored_path << "\n";
                        files_added = false;
                        break;
                    }
                }

                if (!files_added || !main_packer.pack(bundle_name)) {
                    std::cerr << "failed to pack files\n";
                    continue;
                }

                std::cout << "packed " << staged_files.size() << " file(s) into "
                    << bundle_name << ".x\n";
                continue;
            }

            if (command == "unpack") {
                if (arguments.size() < 3 || arguments.size() > 4) {
                    std::cerr << "usage: unpack <bundle> <name> [output]\n";
                    continue;
                }

                const std::string& bundle_path = arguments[1];
                const std::string& stored_path = arguments[2];
                const std::string& output_path = arguments.size() == 4 ? arguments[3] : stored_path;

                bool is_readen = false;
                std::vector<char> buffer = load_file(bundle_path, is_readen);
                if (!is_readen) {
                    std::cerr << "failed to load bundle: " << bundle_path << "\n";
                    continue;
                }

                unpacker main_unpacker{};
                if (!main_unpacker.load_buffer(buffer.data(), buffer.size())) {
                    std::cerr << "unknown or damaged bundle\n";
                    continue;
                }

                u8* data = nullptr;
                u64 data_size = 0;
                if (!main_unpacker.seek_file(stored_path.c_str(), data, data_size)) {
                    std::cerr << "file not found in bundle: " << stored_path << "\n";
                    continue;
                }

                std::vector<char> output(data, data + data_size);
                if (!create_file(output_path.c_str(), output)) {
                    std::cerr << "failed to create file: " << output_path << "\n";
                    continue;
                }

                std::cout << "unpacked " << stored_path << " into " << output_path
                    << " (" << data_size << " bytes)\n";
                continue;
            }

            std::cerr << "unknown command: " << arguments[0] << "\n";
        }
        catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << "\n";
        }
    }

    return 0;
}
