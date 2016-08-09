/*

Osmium -- OpenStreetMap data manipulation command line tool
http://osmcode.org/osmium

Copyright (C) 2013-2016  Jochen Topf <jochen@topf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#include <iostream>
#include <iterator>

#include <boost/program_options.hpp>

#include <osmium/io/any_input.hpp>
#include <osmium/io/any_output.hpp>

#include "command_add_locations_to_ways.hpp"
#include "exception.hpp"

bool CommandAddLocationsToWays::setup(const std::vector<std::string>& arguments) {
    const auto& map_factory = osmium::index::MapFactory<osmium::unsigned_object_id_type, osmium::Location>::instance();
    std::string default_index_type{ map_factory.has_map_type("sparse_mmap_array") ? "sparse_mmap_array" : "sparse_mem_array" };

    po::options_description opts_cmd{"COMMAND OPTIONS"};
    opts_cmd.add_options()
    ("index-type,i", po::value<std::string>()->default_value(default_index_type), "Index type to use")
    ("show-index-types,I", "Show available index types")
    ("keep-untagged-nodes,n", "Keep untagged nodes")
    ;

    po::options_description opts_common{add_common_options()};
    po::options_description opts_input{add_multiple_inputs_options()};
    po::options_description opts_output{add_output_options()};

    po::options_description hidden;
    hidden.add_options()
    ("input-filenames", po::value<std::vector<std::string>>(), "Input files")
    ;

    po::options_description desc;
    desc.add(opts_cmd).add(opts_common).add(opts_input).add(opts_output);

    po::options_description parsed_options;
    parsed_options.add(desc).add(hidden);

    po::positional_options_description positional;
    positional.add("input-filenames", -1);

    po::variables_map vm;
    po::store(po::command_line_parser(arguments).options(parsed_options).positional(positional).run(), vm);
    po::notify(vm);

    if (vm.count("show-index-types")) {
        for (const auto& map_type : map_factory.map_types()) {
            std::cout << map_type << "\n";
        }
        return false;
    }

    if (vm.count("index-type")) {
        m_index_type_name = vm["index-type"].as<std::string>();
        if (!map_factory.has_map_type(m_index_type_name)) {
            throw argument_error(std::string{"Unknown index type '"} + m_index_type_name + "'. Use --show-index-types or -I to get a list.");
        }
    }

    setup_common(vm, desc);
    setup_input_files(vm);
    setup_output_file(vm);

    if (vm.count("keep-untagged-nodes")) {
        m_keep_untagged_nodes = true;
    }

    return true;
}

void CommandAddLocationsToWays::show_arguments() {
    show_multiple_inputs_arguments(m_vout);
    show_output_arguments(m_vout);

    m_vout << "  other options:\n";
    m_vout << "    index type: " << m_index_type_name << "\n";
    m_vout << "    keep untagged nodes: " << (m_keep_untagged_nodes ? "yes" : "no") << "\n";
    m_vout << "\n";
}

void CommandAddLocationsToWays::setup_header(osmium::io::Header& header) const {
    header.set("generator", m_generator);
    for (const auto& h : m_output_headers) {
        header.set(h);
    }
}

void CommandAddLocationsToWays::copy_data(osmium::io::Reader& reader, osmium::io::Writer& writer, location_handler_type& location_handler) {
    while (osmium::memory::Buffer buffer = reader.read()) {
        osmium::apply(buffer, location_handler);

        if (m_keep_untagged_nodes) {
            writer(std::move(buffer));
        } else {
            for (const auto& object : buffer) {
                if (object.type() != osmium::item_type::node || !static_cast<const osmium::Node&>(object).tags().empty()) {
                    writer(object);
                }
            }
        }
    }
}

bool CommandAddLocationsToWays::run() {
    const auto& map_factory = osmium::index::MapFactory<osmium::unsigned_object_id_type, osmium::Location>::instance();
    auto location_index = map_factory.create_map(m_index_type_name);
    location_handler_type location_handler(*location_index);
    location_handler.ignore_errors(); // XXX

    m_output_file.set("locations_on_ways");

    if (m_input_files.size() == 1) { // single input file
        m_vout << "Copying input file '" << m_input_files[0].filename() << "'\n";
        osmium::io::Reader reader(m_input_files[0]);
        osmium::io::Header header = reader.header();
        setup_header(header);
        osmium::io::Writer writer(m_output_file, header, m_output_overwrite, m_fsync);

        copy_data(reader, writer, location_handler);

        writer.close();
        reader.close();
    } else { // multiple input files
        osmium::io::Header header;
        setup_header(header);
        osmium::io::Writer writer(m_output_file, header, m_output_overwrite, m_fsync);

        for (const auto& input_file : m_input_files) {
            m_vout << "Copying input file '" << input_file.filename() << "'\n";
            osmium::io::Reader reader(input_file);

            copy_data(reader, writer, location_handler);

            reader.close();
        }
        writer.close();
    }

    show_memory_used();
    m_vout << "Done.\n";

    return true;
}

namespace {

    const bool register_add_locations_to_ways_command = CommandFactory::add("add-locations-to-ways", "Add node locations to ways", []() {
        return new CommandAddLocationsToWays();
    });

}
