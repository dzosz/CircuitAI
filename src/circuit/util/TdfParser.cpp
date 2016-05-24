/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "util/tdf_grammar.h"
#include "util/TdfParser.h"
#include "CircuitAI.h"
#include "util/utils.h"

#include "System/Util.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <limits.h>
#include <stdexcept>
#include <sstream>

#include <boost/scoped_array.hpp>

void TdfParser::TdfSection::print(std::ostream & out) const
{
	for (std::map<std::string,TdfSection*>::const_iterator it = sections.begin(), e=sections.end(); it != e; ++it) {
		out << "[" << it->first << "]\n{\n";
		it->second->print(out);
		out << "}\n";
	}
	for (std::map<std::string,std::string>::const_iterator it = values.begin(), e=values.end(); it != e; ++it) {
		out << it->first << "=" << it->second << ";\n";
	}
}

TdfParser::TdfSection* TdfParser::TdfSection::construct_subsection(const std::string& name)
{
	std::string lowerd_name = StringToLower(name);
	std::map<std::string,TdfSection*>::iterator it = sections.find(lowerd_name);
	if (it != sections.end()) {
		return it->second;
	} else {
		TdfSection* ret = new TdfSection;
		sections[lowerd_name] = ret;
		return ret;
	}
}

bool TdfParser::TdfSection::remove(const std::string& key, bool caseSensitive)
{
	bool ret = false;

	if (caseSensitive) {
		valueMap_t::iterator it = values.find(key);
		if ((ret = (it != values.end()))) {
			values.erase(it);
		}
	} else {
		// don't assume <key> is already in lowercase
		const std::string lowerKey = StringToLower(key);
		for (valueMap_t::iterator it = values.begin(); it != values.end(); ) {
			if (StringToLower(it->first) == lowerKey) {
				it = set_erase(values, it);
				ret = true;
			} else {
				++it;
			}
		}
	}

	return ret;
}

void TdfParser::TdfSection::add_name_value(const std::string& name, const std::string& value)
{
	std::string lowerd_name = StringToLower(name);
	values[lowerd_name] = value;
}

TdfParser::TdfSection::~TdfSection()
{
	for (std::map<std::string,TdfSection*>::iterator it = sections.begin(), e=sections.end(); it != e; ++it) {
		delete it->second;
	}
}



TdfParser::TdfParser(circuit::CCircuitAI* ai, char const* buf, size_t size)
		: circuit(ai)
		, filename("buffer")
{
	LoadBuffer(buf, size);
}

TdfParser::~TdfParser()
{
}

void TdfParser::print(std::ostream & out) const {
	root_section.print(out);
}


bool TdfParser::parse_buffer(char const* buf, size_t size) {

	std::list<std::string> junk_data;
	tdf_grammar grammar(&root_section, &junk_data);
	parse_info<char const*> info;
	std::string message;
	typedef position_iterator2<char const*> iterator_t;
	iterator_t error_it(buf, buf + size);

	try {
		info = parse(
			buf
			, buf + size
			, grammar
			, space_p
				| comment_p("/*", "*/") // rule for C-comments
				| comment_p("//")
			);
	} catch (const parser_error<tdf_grammar::Errors, char const*>& ex) { // thrown by assertion parsers in tdf_grammar

		switch(ex.descriptor) {
			case tdf_grammar::semicolon_expected: message = "semicolon expected"; break;
			case tdf_grammar::equals_sign_expected: message = "equals sign in name value pair expected"; break;
			case tdf_grammar::square_bracket_expected: message = "square bracket to close section name expected"; break;
			case tdf_grammar::brace_expected: message = "brace or further name value pairs expected"; break;
			default: message = "unknown boost::spirit::parser_error exception"; break;
		};

		std::ptrdiff_t target_pos = ex.where - buf;
		for (int i = 1; i < target_pos; ++i) {
			++error_it;
			if (error_it != (iterator_t(buf + i, buf + size))) {
				++i;
			}
		}

	} catch (...) {
		circuit->LOG("TdfParser: unknown error occurred");
		return false;
	}

	for (std::list<std::string>::const_iterator it = junk_data.begin(), e = junk_data.end(); it !=e ; ++it) {
		std::string temp = StringTrim(*it);
		if (!temp.empty()) {
			circuit->LOG("TdfParser: Junk in %s: %s",
					filename.c_str(), temp.c_str());
		}
	}

	if (!message.empty()) {
		circuit->LOG("Parse error '%s' in %s at line %i column %i near\n%s",
				message.c_str(), filename.c_str(), IntToString(error_it.get_position().line), IntToString(error_it.get_position().column), error_it.get_currentline().c_str());
		return false;
	}

	// a different error might have happened:
	if (!info.full) {
		std::ptrdiff_t target_pos = info.stop - buf;
		for (int i = 1; i < target_pos; ++i) {
			++error_it;
			if (error_it != (iterator_t(buf + i, buf + size))) {
				++i;
			}
		}

		circuit->LOG("Parse error in %s at line %i column %i near\n%s",
				filename.c_str(), IntToString(error_it.get_position().line), IntToString(error_it.get_position().column), error_it.get_currentline().c_str());
		return false;
	}

	return true;
}

void TdfParser::LoadBuffer(char const* buf, size_t size)
{
	this->filename = "buffer";
	parse_buffer(buf, size);
}


std::string TdfParser::SGetValueDef(std::string const& defaultValue, std::string const& location) const
{
	std::string lowerd = StringToLower(location);
	std::string value;
	bool found = SGetValue(value, lowerd);
	if (!found) {
		value = defaultValue;
	}
	return value;
}

bool TdfParser::SGetValue(std::string &value, std::string const& location) const
{
	std::string lowerd = StringToLower(location);
	std::string searchpath; // for error-messages
	// split the location string
	const std::vector<std::string>& loclist = GetLocationVector(lowerd);
	sectionsMap_t::const_iterator sit = root_section.sections.find(loclist[0]);
	if (sit == root_section.sections.end()) {
		value = "Section " + loclist[0] + " missing in file " + filename;
		return false;
	}
	TdfSection* sectionptr = sit->second;
	searchpath = loclist[0];
	for (unsigned int i=1; i < loclist.size()-1; ++i) {
		//const char *arg = loclist[i].c_str();
		searchpath += '\\';
		searchpath += loclist[i];
		sit = sectionptr->sections.find(loclist[i]);
		if (sit == sectionptr->sections.end()) {
			value = "Section " + searchpath + " missing in file " + filename;
			return false;
		}
		sectionptr = sit->second;
	}
	searchpath += '\\';
	searchpath += loclist[loclist.size()-1];

	valueMap_t::const_iterator vit =
		sectionptr->values.find(loclist[loclist.size()-1]);
	if (vit == sectionptr->values.end()) {
		value = "Value " + searchpath + " missing in file " + filename;
		return false;
	}
	std::string svalue = vit->second;
	value = svalue;
	return true;
}

bool TdfParser::GetValue(bool& val, const std::string& location) const
{
	std::string buf;
	if (SGetValue(buf, location)) {
		int tempval;
		std::istringstream stream(buf);
		stream >> tempval;
		if (tempval == 0) {
			val = false;
		} else {
			val = true;
		}
		return true;
	} else {
		return false;
	}
}

const TdfParser::valueMap_t& TdfParser::GetAllValues(std::string const& location) const
{
	static valueMap_t emptymap;
	std::string lowerd = StringToLower(location);
	const std::vector<std::string>& loclist = GetLocationVector(lowerd);
	sectionsMap_t::const_iterator sit = root_section.sections.find(loclist[0]);
	if (sit == root_section.sections.end()) {
		circuit->LOG("Section %s missing in file %s",
				loclist[0].c_str(), filename.c_str());
		return emptymap;
	}
	TdfSection* sectionptr = sit->second;
	std::string searchpath = loclist[0]; // for error-messages
	for (unsigned int i=1; i < loclist.size(); i++) {
		searchpath += '\\';
		searchpath += loclist[i];
		sit = sectionptr->sections.find(loclist[i]);
		if (sit == sectionptr->sections.end()) {
			circuit->LOG("Section %s missing in file %s",
					searchpath.c_str(), filename.c_str());
			return emptymap;
		}
		sectionptr = sit->second;
	}
	return sectionptr->values;
}

std::vector<std::string> TdfParser::GetSectionList(std::string const& location) const
{
	std::string lowerd = StringToLower(location);
	const std::vector<std::string>& loclist = GetLocationVector(lowerd);
	std::vector<std::string> returnvec;
	const sectionsMap_t* sectionsptr = &root_section.sections;
	if (!loclist[0].empty()) {
		std::string searchpath;
		for (unsigned int i = 0; i < loclist.size(); i++) {
			searchpath += loclist[i];
			if (sectionsptr->find(loclist[i]) == sectionsptr->end()) {
				circuit->LOG("Section %s missing in file %s",
						searchpath.c_str(), filename.c_str());
				return returnvec;
			}
			sectionsptr = &sectionsptr->find(loclist[i])->second->sections;
			searchpath += '\\';
		}
	}
	std::map<std::string,TdfSection*>::const_iterator it;
	for (it = sectionsptr->begin(); it != sectionsptr->end(); ++it) {
		returnvec.push_back(it->first);
		StringToLowerInPlace(returnvec.back());
	}
	return returnvec;
}

bool TdfParser::SectionExist(std::string const& location) const
{
	std::string lowerd = StringToLower(location);
	const std::vector<std::string>& loclist = GetLocationVector(lowerd);
	sectionsMap_t::const_iterator sit = root_section.sections.find(loclist[0]);
	if (sit == root_section.sections.end()) {
		return false;
	}
	TdfSection* sectionptr = sit->second;
	for (unsigned int i = 1; i < loclist.size(); i++) {
		sit = sectionptr->sections.find(loclist[i]);
		if (sit == sectionptr->sections.end()) {
			return false;
		}
		sectionptr = sectionptr->sections[loclist[i]];
	}
	return true;
}

std::vector<std::string> TdfParser::GetLocationVector(std::string const& location) const
{
	std::string lowerd = StringToLower(location);
	std::vector<std::string> loclist;
	std::string::size_type start = 0;
	std::string::size_type next = 0;

	while ((next = lowerd.find_first_of("\\", start)) != std::string::npos) {
		loclist.push_back(lowerd.substr(start, next-start));
		start = next + 1;
	}
	loclist.push_back(lowerd.substr(start));

	return loclist;
}

springai::AIFloat3 TdfParser::GetFloat3(springai::AIFloat3 def, std::string const& location) const
{
	std::string s = SGetValueDef("", location);
	if (s.empty()) {
		return def;
	}
	springai::AIFloat3 ret;
	ParseArray(s, &ret.x, 3);
	return ret;
}
