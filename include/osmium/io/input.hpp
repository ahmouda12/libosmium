#ifndef OSMIUM_IO_INPUT_HPP
#define OSMIUM_IO_INPUT_HPP

/*

This file is part of Osmium (http://osmcode.org/osmium).

Copyright 2013 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <cassert>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <osmium/io/file.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/object.hpp>
#include <osmium/osm/entity_flags.hpp>
#include <osmium/thread/queue.hpp>

namespace osmium {

    namespace io {

        namespace detail {

            /**
             * Virtual base class for all classes reading OSM files in different
             * encodings.
             *
             * Do not use this class or derived classes directly. Use the
             * osmium::io::Reader class instead.
             */
            class InputFormat {

            protected:

                osmium::io::File m_file;
                osmium::osm_entity::flags m_read_which_entities;
                osmium::thread::Queue<std::string>& m_input_queue;
                osmium::io::Header m_header {};

                InputFormat(const osmium::io::File& file, osmium::osm_entity::flags read_which_entities, osmium::thread::Queue<std::string>& input_queue) :
                    m_file(file),
                    m_read_which_entities(read_which_entities),
                    m_input_queue(input_queue) {
                    m_header.has_multiple_object_versions(m_file.has_multiple_object_versions());
                }

                InputFormat(const InputFormat&) = delete;
                InputFormat(InputFormat&&) = delete;

                InputFormat& operator=(const InputFormat&) = delete;
                InputFormat& operator=(InputFormat&&) = delete;

            public:

                virtual ~InputFormat() {
                }

                virtual void open() = 0;

                virtual osmium::memory::Buffer read() = 0;

                virtual void close() {
                }

                osmium::io::Header header() const {
                    return m_header;
                }

            }; // class InputFormat

            /**
             * This factory class is used to create objects that read OSM data
             * written in a specified format.
             *
             * Do not use this class directly. Instead use the osmium::io::Reader
             * class.
             */
            class InputFormatFactory {

            public:

                typedef std::function<osmium::io::detail::InputFormat*(const osmium::io::File&, osmium::osm_entity::flags read_which_entities, osmium::thread::Queue<std::string>&)> create_input_type;

            private:

                typedef std::map<osmium::io::Encoding*, create_input_type> encoding2create_type;

                encoding2create_type m_callbacks;

                InputFormatFactory() :
                    m_callbacks() {
                }

            public:

                static InputFormatFactory& instance() {
                    static InputFormatFactory factory;
                    return factory;
                }

                bool register_input_format(std::vector<osmium::io::Encoding*> encodings, create_input_type create_function) {
                    for (auto encoding : encodings) {
                        if (! m_callbacks.insert(encoding2create_type::value_type(encoding, create_function)).second) {
                            return false;
                        }
                    }
                    return true;
                }

                std::unique_ptr<osmium::io::detail::InputFormat> create_input(const osmium::io::File& file, osmium::osm_entity::flags read_which_entities, osmium::thread::Queue<std::string>& input_queue) {
                    encoding2create_type::iterator it = m_callbacks.find(file.encoding());

                    if (it != m_callbacks.end()) {
                        return std::unique_ptr<osmium::io::detail::InputFormat>((it->second)(file, read_which_entities, input_queue));
                    }

                    throw std::runtime_error(std::string("Unknown encoding for input: ") + file.encoding()->suffix());
                }

            }; // class InputFormatFactory

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_INPUT_HPP
