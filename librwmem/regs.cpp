#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <exception>

#include "regs.h"
#include "helpers.h"

using namespace std;

Register::Register(const RegisterFileData* rfd, const RegisterBlockData* rbd, const RegisterData* rd)
	: m_rfd(rfd), m_rbd(rbd), m_rd(rd)
{
}

Field Register::at(uint32_t idx) const
{
	if (idx >= m_rd->num_fields())
		throw runtime_error("field idx too high");

	const FieldData* fd = m_rd->at(m_rfd, idx);
	return Field(m_rfd, fd);
}

int Register::find_field(const string& name) const
{
	for (unsigned i = 0; i < num_fields(); ++i) {
		Field f = at(i);

		if (strcmp(f.name(), name.c_str()) == 0)
			return i;
	}

	return -1;
}

Field Register::get_field(const string& name) const
{
	int i = find_field(name);

	if (i < 0)
		throw runtime_error("field not found");

	return at(i);
}

Field Register::get_field(uint8_t high, uint8_t low) const
{
	for (unsigned i = 0; i < num_fields(); ++i) {
		Field f = at(i);
		if (low == f.low() && high == f.high())
			return f;
	}

	throw runtime_error("field not found");
}

RegisterBlock Register::register_block() const
{
	return RegisterBlock(m_rfd, m_rbd);
}

Register RegisterBlock::at(uint32_t idx) const
{
	if (idx >= m_rbd->num_regs())
		throw runtime_error("register idx too high");

	const RegisterData* rd = m_rbd->at(m_rfd, idx);
	return Register(m_rfd, m_rbd, rd);
}

int RegisterBlock::find_register(const string& name) const
{
	for (unsigned i = 0; i < num_regs(); ++i) {
		Register reg = this->at(i);

		if (strcmp(reg.name(), name.c_str()) == 0)
			return i;
	}

	return -1;
}

Register RegisterBlock::get_register(const string& name) const
{
	int i = find_register(name);

	if (i < 0)
		throw runtime_error("register not found");

	return at(i);
}


RegisterFile::RegisterFile(const std::string& filename)
{
	int fd = open(filename.c_str(), O_RDONLY);
	ERR_ON_ERRNO(fd < 0, "Open regfile '%s' failed", filename.c_str());

	off_t len = lseek(fd, (size_t)0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	void* data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	ERR_ON_ERRNO(data == MAP_FAILED, "mmap regfile failed");

	m_rfd = (RegisterFileData*)data;
	m_size = len;
}

RegisterFile::~RegisterFile()
{
	munmap((void*)m_rfd, m_size);
}

RegisterBlock RegisterFile::at(uint32_t idx) const
{
	if (idx >= m_rfd->num_blocks())
		throw runtime_error("register block idx too high");

	const RegisterBlockData* rbd = m_rfd->at(idx);
	return RegisterBlock(m_rfd, rbd);
}

RegisterBlock RegisterFile::get_register_block(const string& name) const
{
	const RegisterBlockData* rb = m_rfd->find_block(name);

	if (rb)
		return RegisterBlock(m_rfd, rb);

	throw runtime_error("register block not found");
}

Register RegisterFile::get_register(const string& name) const
{
	for (unsigned bidx = 0; bidx < num_blocks(); ++bidx) {
		const RegisterBlock rb = at(bidx);

		for (unsigned ridx = 0; ridx < rb.num_regs(); ++ridx) {
			Register reg = rb.at(ridx);

			if (strcmp(reg.name(), name.c_str()) == 0)
				return reg;
		}
	}

	throw runtime_error("register not found");
}

Register RegisterFile::get_register(uint64_t offset) const
{
	for (unsigned bidx = 0; bidx < num_blocks(); ++bidx) {
		const RegisterBlock rb = at(bidx);

		if (offset < rb.offset())
			continue;

		if (offset >= rb.offset() + rb.size())
			continue;

		for (unsigned ridx = 0; ridx < rb.num_regs(); ++ridx) {
			Register reg = rb.at(ridx);

			if (reg.offset() == offset - rb.offset())
				return reg;
		}
	}

	throw runtime_error("register not found");
}

static void print_regfile(const RegisterFile& rf)
{
	printf("%s: total %u/%u/%u\n", rf.name(), rf.num_blocks(), rf.num_regs(), rf.num_fields());
}

static void print_register_block(const RegisterBlock& rb)
{
	printf("  %s: %#" PRIx64 " %#" PRIx64 ", regs %u\n", rb.name(), rb.offset(), rb.size(), rb.num_regs());
}

static void print_register(const Register& reg)
{
	printf("    %s: %#" PRIx64 " %#x, fields %u\n", reg.name(), reg.offset(), reg.size(), reg.num_fields());
}

static void print_field(const Field& field)
{
	printf("      %s: %u:%u\n", field.name(), field.high(), field.low());
}

static void print_all(const RegisterFile& rf)
{
	print_regfile(rf);

	for (unsigned bidx = 0; bidx < rf.num_blocks(); ++bidx) {
		RegisterBlock rb = rf.at(bidx);
		print_register_block(rb);

		for (unsigned ridx = 0; ridx < rb.num_regs(); ++ridx) {
			Register reg = rb.at(ridx);
			print_register(reg);

			for (unsigned fidx = 0; fidx < reg.num_fields(); ++fidx) {
				Field field = reg.at(fidx);
				print_field(field);
			}
		}
	}
}

static void print_pattern(const RegisterFile& rf, const string& pattern)
{
	bool regfile_printed = false;

	for (unsigned bidx = 0; bidx < rf.num_blocks(); ++bidx) {
		RegisterBlock rb = rf.at(bidx);

		bool block_printed = false;

		for (unsigned ridx = 0; ridx < rb.num_regs(); ++ridx) {
			Register reg = rb.at(ridx);

			if (strcasestr(reg.name(), pattern.c_str()) == NULL)
				continue;

			if (!regfile_printed) {
				print_regfile(rf);
				regfile_printed = true;
			}

			if (!block_printed) {
				print_register_block(rb);
				block_printed = true;
			}

			print_register(reg);

			for (unsigned fidx = 0; fidx < reg.num_fields(); ++fidx) {
				Field field = reg.at(fidx);

				print_field(field);
			}
		}
	}
}

void RegisterFile::print(const string& pattern)
{
	if (pattern.empty())
		print_all(*this);
	else
		print_pattern(*this, pattern);
}