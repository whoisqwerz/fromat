#ifndef PACK_H
#define PACK_H
#define NOMINMAX
#define PACKER

#include <windows.h>
#include <cstdint>

#ifdef PACKER
#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>
#endif // !PACKER

using u8 = uint8_t;
using u16 = unsigned short;
using u32 = uint32_t;
using u64 = uint64_t;

template<typename T>
__forceinline u64 to_addr(T* v) { return u64(v); };
__forceinline u64 to_addr(u64 v) { return u64(v); };

#define FNV_OFFSET_64 14695981039346656037ULL
#define FNV_PRIME_64 1099511628211ULL
#define FORMAT_MAGIC 'UFMT'
#define FORMAT_VERSION '00.1'
#define IS_TARGET_OUT_OF(target, max) to_addr(target) > to_addr(max)
#define IS_TARGET_BEFORE(target, max) to_addr(target) < to_addr(max)
#define VALIDATE_TABLE(S, MASK) validate_table(S, metadata.MASK##_offset, metadata.MASK##_size);

constexpr u64 fnv1a_hash_64(const void* data, size_t num_bytes) {
    const u8* byte = (const u8*)data;
    u64 hash = FNV_OFFSET_64;

    for (size_t i = 0; i < num_bytes; i++) {
        hash ^= byte[i];
        hash *= FNV_PRIME_64;
    }

    return hash;
}

__forceinline u64 __strlen(const char* str) {
    if (!str) { return 0; }
    const char* counter = str;
    while (*str != '\0') {
        str++;
    }
    return static_cast<u64>(str - counter);
};

struct header {
    u32 magic;
    u32 version;
};

struct metadata {
    u64 file_count;
    u64 strings_offset;
    u64 strings_size;
    u64 data_offset;
    u64 data_size;
    u64 hashtable_offset;
    u64 hashtable_size;
    u64 file_table_offset;
    u64 file_table_size;
};

struct hashtable_entry {
    u64 hash;
    u64 file_offset;
    bool occupied;
};

struct hashtable {
    u64 count;
    hashtable_entry entries[1];
    bool insert(u64 hash, u64 file_offset) {
        if (count == 0) {
            return false;
        }

        u64 hash_point = hash % count;
        auto& entry = entries[hash_point];
        if (entry.occupied) {
            if (entry.hash == hash) {
                return false;
            }
            
            for (u64 hash_entry_id = 0; hash_entry_id < count; hash_entry_id++) {
                auto& entry = entries[(hash_point + hash_entry_id) % count];
                if (entry.occupied) {
                    continue;
                }

                entry = hashtable_entry(hash, file_offset, true);
                return true;
            }
            
            return false;
        }

        entries[hash_point] = hashtable_entry(hash, file_offset, true);
        return true;
    }

    u64 find_file(const char* path) {
        if (count <= 0) {
            return 0;
        }

        u64 hash = fnv1a_hash_64(path, __strlen(path));
        u64 hash_point = hash % count;
        auto& hashtable_entry = entries[hash_point];
        if (hashtable_entry.occupied) {
            if (hashtable_entry.hash == hash) {
                return hashtable_entry.file_offset;
            }

            for (u64 hash_entry_id = 0; hash_entry_id < count; hash_entry_id++) {
                auto& entry = entries[(hash_point + hash_entry_id) % count];
                if (entry.occupied && entry.hash == hash) {
                    return entry.file_offset;
                }
            }

            return 0;
        }

        return 0;
    }
};

struct file_entry {
    u64 id;
    u64 name_offset;
    u64 name_size;
    u64 data_offset;
    u64 data_size;
    u8 hash[32]; // not implemented
};

struct bundle {
    header header;
    metadata metadata;

    bool validate_headers() { return header.magic == FORMAT_MAGIC && header.version == FORMAT_VERSION; };
    bool validate_binary(u64 binary_size) {
        u64 binary_start = reinterpret_cast<u64>(this);
        u64 binary_end = binary_start + binary_size;

        if (!validate_tables(binary_size)) {
            return false;
        }

        if (!validate_tables_data(binary_start, binary_end)) {
            return false;
        }

        return true;
    }
private:
    bool validate_tables(u64 binary_size) {
        bool result = true;
        result &= VALIDATE_TABLE(binary_size, strings);
        result &= VALIDATE_TABLE(binary_size, data);
        result &= VALIDATE_TABLE(binary_size, hashtable);
        result &= VALIDATE_TABLE(binary_size, file_table);
        return result;
    }

    bool validate_tables_data(u64 binary_start, u64 binary_end) {
        bool result = true;
        result &= validate_file_table_data(binary_start, binary_end);
        result &= validate_hash_table_data(binary_start, binary_end);
        return result;
    }

    bool is_valid_range(u64 binary_size, u64 offset, u64 size) {
        return offset >= sizeof(bundle) && offset <= binary_size && size <= binary_size - offset;
    }

    bool validate_table(u64 binary_size, u64 offset, u64 size) {
        return is_valid_range(binary_size, offset, size);
    }

    bool validate_file_table_data(u64 binary_start, u64 binary_end) {
        u64 file_table_start = binary_start + metadata.file_table_offset;
        u64 file_table_end = binary_start + metadata.file_table_offset + metadata.file_table_size;
        u64 file_count = metadata.file_count;
        u64 binary_size = binary_end - binary_start;

        if (file_count > metadata.file_table_size / sizeof(file_entry)) {
            return false;
        }

        if (!is_valid_range(binary_size, metadata.strings_offset, metadata.strings_size) ||
            !is_valid_range(binary_size, metadata.data_offset, metadata.data_size)) {
            return false;
        }

        u64 strings_start = binary_start + metadata.strings_offset;
        u64 data_start = binary_start + metadata.data_offset;

        u64 strings_end = strings_start + metadata.strings_size;
        u64 data_end = data_start + metadata.data_size;

        file_entry* file_table = reinterpret_cast<file_entry*>(file_table_start);
        
        for (u64 file_id = 0; file_id < file_count; file_id++) {
            const file_entry& file = file_table[file_id];
            
            if (!is_valid_range(binary_size, file.name_offset, file.name_size) ||
                !is_valid_range(binary_size, file.data_offset, file.data_size)) {
                return false;
            }

            u64 file_name_start = binary_start + file.name_offset;
            u64 file_data_start = binary_start + file.data_offset;
            u64 file_name_end = file_name_start + file.name_size;
            u64 file_data_end = file_data_start + file.data_size;

            if (IS_TARGET_OUT_OF(file_name_end, strings_end) ||
                IS_TARGET_OUT_OF(file_name_end, binary_end) ||
                IS_TARGET_OUT_OF(file_data_end, data_end) ||
                IS_TARGET_OUT_OF(file_data_end, binary_end) || 
                IS_TARGET_BEFORE(file_name_start, strings_start) ||
                IS_TARGET_BEFORE(file_data_start, data_start)) {
                return false;
            }
        }

        return true;
    }

    bool validate_hash_table_data(u64 binary_start, u64 binary_end) {
        u64 binary_size = binary_end - binary_start;
        u64 hash_table_start = binary_start + metadata.hashtable_offset;
        u64 hash_table_end = binary_start + metadata.hashtable_offset + metadata.hashtable_size;

        if (metadata.hashtable_size < sizeof(hashtable::count)) {
            return false;
        }

        hashtable* hash_table = reinterpret_cast<hashtable*>(hash_table_start);
        u64 hash_table_count = hash_table->count;
        u64 entries_size = metadata.hashtable_size - sizeof(hashtable::count);
        if (hash_table_count > entries_size / (sizeof(hashtable_entry))) {
            return false;
        }

        u64 file_table_start = binary_start + metadata.file_table_offset;
        u64 file_table_end = file_table_start + metadata.file_table_size;

        for (u64 hash_table_entry_id = 0; hash_table_entry_id < hash_table_count; hash_table_entry_id++) {
            hashtable_entry& entry = hash_table->entries[hash_table_entry_id];
            if (!is_valid_range(binary_size, entry.file_offset, sizeof(file_entry))) {
                return false;
            }

            u64 file_entry_ = binary_start + entry.file_offset;
            u64 file_entry_end = file_entry_ + sizeof(file_entry);

            if (IS_TARGET_OUT_OF(file_entry_end, file_table_end) ||
                IS_TARGET_OUT_OF(file_entry_end, binary_end) ||
                IS_TARGET_BEFORE(file_entry_, file_table_start)) {
                return false;
            }
        }

        return true;
    }
};

#ifdef PACKER
struct raw_file {
    std::string path;
    u8* data;
    u64 size;
    u64 hash;
    u64 string_table_offset;
    u64 data_table_offset;
};

class packer {
public:
    std::vector<raw_file> files;

    bool add_file(const std::string& path, u8* data, u64 size) {
        if (!size) {
            return false;
        }

        u64 hash = fnv1a_hash_64(path.data(), path.size());
        files.push_back({ path, data, size, hash });
        return true;
    }

    bool pack(const std::string& bundle_name) {

        u64 hashtable_offset = sizeof(bundle);
        u64 file_count = files.size();

        u64 overall_data_size = 0;
        u64 overall_strings_size = 0;
        u64 overall_hashtable_size = sizeof(u64) + sizeof(hashtable_entry) * file_count;
        u64 overall_file_table_size = sizeof(file_entry) * file_count;

        const header h = { FORMAT_MAGIC , FORMAT_VERSION };
        const metadata meta = { .file_count = file_count, .hashtable_offset = hashtable_offset };
        auto main_bundle = std::make_unique<bundle>(h, meta);

        std::vector<file_entry> file_table(file_count);
        u64 file_table_offset = hashtable_offset + overall_hashtable_size;
        main_bundle->metadata.file_table_offset = file_table_offset;

        std::vector<u8> hashtable_vector(overall_hashtable_size);
        hashtable* hash_table = reinterpret_cast<hashtable*>(hashtable_vector.data());
        hash_table->count = file_count;

        for (u64 file_id = 0; file_id < files.size(); file_id++) {
            raw_file& file = files[file_id];
            overall_data_size += file.size;
            overall_strings_size += file.path.size() + 1;

            bool insert_success = hash_table->insert(file.hash, file_table_offset + file_id * sizeof(file_entry));
            if (!insert_success) {
                throw std::runtime_error("failed to insert file in hashtable");
            }
        }

        main_bundle->metadata.strings_size = overall_strings_size;
        main_bundle->metadata.data_size = overall_data_size;
        main_bundle->metadata.hashtable_size = overall_hashtable_size;
        main_bundle->metadata.file_table_size = overall_file_table_size;
        
        std::vector<char> string_table(overall_strings_size);
        std::vector<u8> data_table(overall_data_size);
       
        u64 string_cursor = 0;
        u64 data_cursor = 0;
        for (u64 file_id = 0; file_id < files.size(); file_id++) {
            auto& file = files[file_id];

            std::memcpy(string_table.data() + string_cursor, file.path.data(), file.path.size() + 1);
            std::memcpy(data_table.data() + data_cursor, file.data, file.size);

            file.string_table_offset = string_cursor;
            file.data_table_offset = data_cursor;

            string_cursor += file.path.size() + 1;
            data_cursor += file.size;
        }

        u64 string_table_offset = file_table_offset + overall_file_table_size;
        u64 data_table_offset = string_table_offset + string_cursor;

        main_bundle->metadata.strings_offset = string_table_offset;
        main_bundle->metadata.data_offset = data_table_offset;

        for (u64 file_id = 0; file_id < file_count; file_id++) {
            auto& file = files[file_id];
            auto& file_table_entry = file_table[file_id];

            u64 name_offset = string_table_offset + file.string_table_offset;
            u64 data_offset = data_table_offset + file.data_table_offset;

            file_table_entry.id = file_id;
            file_table_entry.name_offset = name_offset;
            file_table_entry.name_size = file.path.size();
            file_table_entry.data_offset = data_offset;
            file_table_entry.data_size = file.size;
        }

        u64 final_size = sizeof(bundle) + overall_hashtable_size + overall_file_table_size + overall_strings_size + overall_data_size;
        std::vector<u8> final_bundle(final_size);
        
        std::memcpy(final_bundle.data(),  main_bundle.get(), sizeof(bundle));
        std::memcpy(final_bundle.data() + hashtable_offset, hashtable_vector.data(), hashtable_vector.size());
        std::memcpy(final_bundle.data() + file_table_offset, file_table.data(), file_table.size() * sizeof(file_entry));
        std::memcpy(final_bundle.data() + string_table_offset, string_table.data(), string_table.size());
        std::memcpy(final_bundle.data() + data_table_offset, data_table.data(), data_table.size());

        std::ofstream out_file(bundle_name + ".x", std::ios::out | std::ios::binary);
        if (!out_file) {
            throw std::runtime_error("failed to create bundle file");
        }

        out_file.write(reinterpret_cast<const char*>(final_bundle.data()), final_bundle.size());
        out_file.close();
        
        return true;
    }
};
#endif // !PACKER

class unpacker {
public:
    bundle* main_bundle;
    u64 main_bundle_size;

    bool load_buffer(void* buffer_base, u64 size) {
        if (!buffer_base || size < sizeof(bundle)) {
            return false;
        }

        bundle* bun = reinterpret_cast<bundle*>(buffer_base);
        if (!bun->validate_headers()) {
            return false;
        }

        if (!bun->validate_binary(size)) {
            return false;
        }

        main_bundle = bun;
        main_bundle_size = size;
        return true;
    }

    bool seek_file(const char* path, u8*& data_out, u64& size_out) {
        u64 bundle_base_address = reinterpret_cast<u64>(main_bundle);
        u64 bundle_end_address = bundle_base_address + main_bundle_size;

        hashtable* hashtable_base = reinterpret_cast<hashtable*>(bundle_base_address + main_bundle->metadata.hashtable_offset);
        if (IS_TARGET_OUT_OF(hashtable_base, bundle_end_address)) {
            return false;
        }

        u64 file_entry_offset = hashtable_base->find_file(path);
        if (!file_entry_offset) {
            return false;
        }

        file_entry* file = reinterpret_cast<file_entry*>(bundle_base_address + file_entry_offset);
        if (IS_TARGET_OUT_OF(file, bundle_end_address)) {
            return false;
        }

        size_out = file->data_size;
        data_out = reinterpret_cast<u8*>(bundle_base_address + file->data_offset);

        if (IS_TARGET_OUT_OF(data_out, bundle_end_address)) {
            return false;
        }

        return true;
    }
};

#endif // !PACK_H
