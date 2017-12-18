#ifndef OSMIUM_INDEX_MAP_FLEX_MEM_HPP
#define OSMIUM_INDEX_MAP_FLEX_MEM_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2017 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <osmium/index/index.hpp>
#include <osmium/index/map.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#define OSMIUM_HAS_INDEX_MAP_FLEX_MEM

namespace osmium {

    namespace index {

        namespace map {

            /**
             * This is an autoscaling index that works well with small and
             * large input data. All data will be held in memory. For small
             * input data a sparse array will be used, if this becomes
             * inefficient, the class will switch automatically to a dense
             * index.
             */
            template <typename TId, typename TValue>
            class FlexMem : public osmium::index::map::Map<TId, TValue> {

                // This value is based on benchmarks with a planet file and
                // some smaller files.
                enum constant_bits {
                    bits = 16
                };

                enum constant_block_size : uint64_t {
                    block_size = 1ll << bits
                };

                // Minimum number of entries in the sparse index before we
                // are considering switching to a dense index.
                enum constant_min_dense_entries : int64_t {
                    min_dense_entries = 0xffffff
                };

                // When more than a third of all Ids are in the index, we
                // switch to the dense index. This is a compromise between
                // the best memory efficiency (which we would get at a factor
                // of 2) and the performance (dense index is much faster then
                // the sparse index).
                enum constant_density_factor {
                    density_factor = 3
                };

                // An entry in the sparse index
                struct entry {
                    uint64_t id;
                    TValue value;

                    entry(uint64_t i, TValue v) :
                        id(i),
                        value(std::move(v)) {
                    }

                    bool operator<(const entry other) const noexcept {
                        return id < other.id;
                    }
                };

                std::vector<entry> m_sparse_entries;

                std::vector<std::vector<TValue>> m_dense_blocks;

                // The maximum Id that was seen yet. Only set in sparse mode.
                uint64_t m_max_id = 0;

                // Set to false in sparse mode and to true in dense mode.
                bool m_dense;

                static uint64_t block(const uint64_t id) noexcept {
                    return id >> bits;
                }

                static uint64_t offset(const uint64_t id) noexcept {
                    return id & (block_size - 1);
                }

                // Assure that the block with the given number exists. Create
                // it if needed.
                void assure_block(const uint64_t num) {
                    if (num >= m_dense_blocks.size()) {
                        m_dense_blocks.resize(num + 1);
                    }
                    if (m_dense_blocks[num].empty()) {
                        m_dense_blocks[num].assign(block_size, osmium::index::empty_value<TValue>());
                    }
                }

                void set_sparse(const uint64_t id, const TValue value) {
                    m_sparse_entries.emplace_back(id, value);
                    if (id > m_max_id) {
                        m_max_id = id;

                        if (m_sparse_entries.size() >= min_dense_entries) {
                            if (m_max_id < m_sparse_entries.size() * density_factor) {
                                switch_to_dense();
                            }
                        }
                    }
                }

                TValue get_sparse(const uint64_t id) const noexcept {
                    const auto it = std::lower_bound(m_sparse_entries.begin(),
                                                     m_sparse_entries.end(),
                                                     entry{id, osmium::index::empty_value<TValue>()});
                    if (it == m_sparse_entries.end() || it->id != id) {
                        return osmium::index::empty_value<TValue>();
                    }
                    return it->value;
                }

                void set_dense(const uint64_t id, const TValue value) {
                    assure_block(block(id));
                    m_dense_blocks[block(id)][offset(id)] = value;
                }

                TValue get_dense(const uint64_t id) const noexcept {
                    if (m_dense_blocks.size() <= block(id) || m_dense_blocks[block(id)].empty()) {
                        return osmium::index::empty_value<TValue>();
                    }
                    return m_dense_blocks[block(id)][offset(id)];
                }

            public:

                /**
                 * Create FlexMem index.
                 *
                 * @param use_dense Usually FlexMem indexes start out as sparse
                 *                  indexes and will switch to dense when they
                 *                  think it is better. Set this to force dense
                 *                  indexing from the start. This is usually
                 *                  only useful for testing.
                 */
                explicit FlexMem(bool use_dense = false) :
                    m_dense(use_dense) {
                }

                bool is_dense() const noexcept {
                    return m_dense;
                }

                std::size_t size() const noexcept final {
                    if (m_dense) {
                        return m_dense_blocks.size() * block_size;
                    }
                    return m_sparse_entries.size();
                }

                std::size_t used_memory() const noexcept final {
                    return sizeof(FlexMem) +
                           m_sparse_entries.size() * sizeof(entry) +
                           m_dense_blocks.size() * (block_size * sizeof(TValue) + sizeof(std::vector<TValue>));
                }

                void set(const TId id, const TValue value) final {
                    if (m_dense) {
                        set_dense(id, value);
                    } else {
                        set_sparse(id, value);
                    }
                }

                TValue get_noexcept(const TId id) const noexcept final {
                    if (m_dense) {
                        return get_dense(id);
                    }
                    return get_sparse(id);
                }

                TValue get(const TId id) const final {
                    const auto value = get_noexcept(id);
                    if (value == osmium::index::empty_value<TValue>()) {
                        throw osmium::not_found{id};
                    }
                    return value;
                }

                void clear() final {
                    m_sparse_entries.clear();
                    m_sparse_entries.shrink_to_fit();
                    m_dense_blocks.clear();
                    m_dense_blocks.shrink_to_fit();
                    m_max_id = 0;
                    m_dense = false;
                }

                void sort() final {
                    std::sort(m_sparse_entries.begin(), m_sparse_entries.end());
                }

                /**
                 * Switch from using a sparse to a dense index. Usually you
                 * do not need to call this, because the FlexMem class will
                 * do this automatically if it thinks the dense index is more
                 * efficient.
                 *
                 * Does nothing if the index is already in dense mode.
                 */
                void switch_to_dense() {
                    if (m_dense) {
                        return;
                    }
                    for (const auto entry : m_sparse_entries) {
                        set_dense(entry.id, entry.value);
                    }
                    m_sparse_entries.clear();
                    m_sparse_entries.shrink_to_fit();
                    m_max_id = 0;
                    m_dense = true;
                }

                std::pair<std::size_t, std::size_t> stats() const noexcept {
                    std::size_t used_blocks = 0;
                    std::size_t empty_blocks = 0;

                    for (const auto& block : m_dense_blocks) {
                        if (block.empty()) {
                            ++empty_blocks;
                        } else {
                            ++used_blocks;
                        }
                    }

                    return std::make_pair(used_blocks, empty_blocks);
                }

            }; // class FlexMem

        } // namespace map

    } // namespace index

} // namespace osmium

#ifdef OSMIUM_WANT_NODE_LOCATION_MAPS
    REGISTER_MAP(osmium::unsigned_object_id_type, osmium::Location, osmium::index::map::FlexMem, flex_mem)
#endif

#endif // OSMIUM_INDEX_MAP_FLEX_MEM_HPP
