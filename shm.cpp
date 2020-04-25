#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <iostream>

#include "shm.h"

using namespace std::chrono;

Shm::Shm(int option, const char *name, size_t buffer_size, size_t buffer_count) : _buffer_size(buffer_size), _buffer_count(buffer_count)
{
	this->_status = Status::s_error;

	this->_name[0] = '/';
	strncpy(&this->_name[1], name, max_name);

	switch (option)
	{
		case Option::o_create:

			if (this->create() == 0)
			{
				this->_status = Status::s_ok;
			}

		break;

		case Option::o_open:

			if (this->open() == 0)
			{
				this->_status = Status::s_ok;
			}

		break;

		case Option::o_create_or_open:

			if (!this->check())
			{
				if (this->create() == 0)
				{
					this->_status = Status::s_ok;
				}
			}
			else
			{
				if (this->open() == 0)
				{
					this->_status = Status::s_ok;
				}
			}

		break;

		default:;
	}
}

int Shm::create()
{
	if (this->_buffer_size < 1 || this->_buffer_count < 1)
	{
		this->_status |= Status::s_range_out;
		return -1;
	}

	if ((this->file_d = shm_open(this->_name, O_CREAT | O_EXCL | O_RDWR, 0666)) == -1)
	{
		return -1;
	}

	this->shm_total_size();

	if (ftruncate(this->file_d, this->total_size) != 0 || this->memory_init(true) != 0)
	{
		::close(this->file_d);
		this->close();
		this->remove(this->_name);
		return -1;
	}

	return 0;
}

int Shm::open()
{
	if ((this->file_d = shm_open(this->_name, O_RDWR, 0666)) == -1)
	{
		return -1;
	}

	struct stat buf;

	if (fstat(this->file_d, &buf) == -1)
	{
		::close(this->file_d);
		return -1;
	}

	this->total_size = buf.st_size;

	if (this->memory_init(false) != 0)
	{
		::close(this->file_d);
		this->close();
		return -1;
	}

	return 0;
}

int Shm::memory_init(bool is_first)
{
	this->address = mmap(NULL, this->total_size, PROT_READ | PROT_WRITE, MAP_SHARED, this->file_d, 0);

	::close(this->file_d);

	if (this->address == MAP_FAILED)
	{
		return -1;
	}

	this->buffer = static_cast<char *>(address);

	if (!this->shm_exist_flag().is_lock_free() || !this->shm_write_index().is_lock_free())
	{
		return -1;
	}

	if (is_first)
	{
		this->shm_buffer_size() = this->_buffer_size;
		this->shm_buffer_count() = this->_buffer_count;
		this->shm_data_time(0) = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
		this->shm_write_index().store(1);
		this->shm_exist_flag().store(true);
	}
	else
	{
		if (!this->shm_exist_flag().load())
		{
			this->_status |= Status::s_not_exist;
			return -1;
		}

		this->_buffer_size = this->shm_buffer_size();
		this->_buffer_count = this->shm_buffer_count();
	}

	return 0;
}

int Shm::close()
{
	if (this->address == nullptr)
	{
		return 0;
	}

	if (munmap(this->address, this->total_size) == -1)
	{
		return -1;
	}

	this->address = nullptr;

	return 0;
}

int Shm::remove()
{
	this->shm_exist_flag().store(false);

	if (this->close() != 0)
	{
		return -1;
	}

	return shm_unlink(this->_name);
}

int Shm::remove(const char *name)
{
	return shm_unlink(std::string('/' + std::string(name)).c_str());
}

bool Shm::check()
{
	return this->check(&this->_name[1]);
}

bool Shm::check(const char *name)
{
	struct stat buffer;
	return (stat(std::string("/dev/shm/" + std::string(name)).c_str(), &buffer) == 0);
}

/*******************************************************************************************

										WRITE

*******************************************************************************************/

int Shm::_write_start(size_t &current_buffer, char &write_mark)
{
	this->_status = Status::s_ok;

	if (!this->address)
	{
		this->_status = Status::s_error;
		return -1;
	}

	if (!this->shm_exist_flag().load())
	{
		this->_status = Status::s_error | Status::s_not_exist;
		return -1;
	}

	size_t write_index = this->shm_write_index().load();

	current_buffer = write_index % this->_buffer_count;

	write_mark = 1 + (write_index % 127);

	this->shm_data_guard(current_buffer)->prefix.store(write_mark);

	this->shm_data_guard(current_buffer)->index.store(write_index);

	this->shm_data_time(current_buffer) = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

	return 0;
}

void Shm::_write_end(size_t current_buffer, char write_mark)
{
	this->shm_data_guard(current_buffer)->postfix.store(write_mark);
	this->shm_write_index().fetch_add(1);
}

void Shm::write(const char *data, size_t size)
{
	auto start = this->chronometry_start();

	size_t current_buffer;
	char write_mark;

	if (this->_write_start(current_buffer, write_mark) != 0) return;

	if (size > this->_buffer_size)
	{
		size = this->_buffer_size;
		this->_status |= Status::s_range_out;
	}

	memcpy(this->shm_data(current_buffer), data, size);

	this->_write_end(current_buffer, write_mark);
	this->chronometry_end(start, this->min_write_time, this->max_write_time);
}

/*******************************************************************************************

										READ

*******************************************************************************************/

template<typename F>
unsigned Shm::_read(F read_functor, Read type, size_t &size)
{
	auto start = this->chronometry_start();

	this->_status = Status::s_ok;

	if (!this->shm_exist_flag().load())
	{
		this->_status = Status::s_error | Status::s_not_exist;
		return this->index;
	}

	if (this->index == 0)
	{
		type = Read::last;
	}

	unsigned index_t = this->index;

	this->index = this->shm_write_index().load();

	if (index_t == this->index)
	{
		this->_status = Status::s_error | Status::s_no_data;
		return this->index;
	}

	if (size > this->_buffer_size)
	{
		size = this->_buffer_size;
		this->_status |= Status::s_range_out;
	}

	unsigned buffer_index = 0;

	if (type == Read::next)
	{
		if (this->index - index_t > this->_buffer_count)
		{
			this->_status |= Status::s_data_loss;
			index_t = this->index - 1;
		}

		buffer_index = index_t;
	}

	if (type == Read::last)
	{
		buffer_index = this->index - 1;
	}

	unsigned current_buffer = buffer_index % this->_buffer_count;

	char _prefix = this->shm_data_guard(current_buffer)->prefix.load();
	char _postfix = this->shm_data_guard(current_buffer)->postfix.load();

	if (_prefix != _postfix)
	{
		this->_status = Status::s_error | Status::s_is_writing;
		return this->index;
	}

	unsigned data_index = this->shm_data_guard(current_buffer)->index.load();

	this->last_read_time = this->shm_data_time(current_buffer);

	read_functor(current_buffer);

	char prefix_ = this->shm_data_guard(current_buffer)->prefix.load();
	char postfix_ = this->shm_data_guard(current_buffer)->postfix.load();

	if (_prefix != _postfix || _postfix != prefix_ || prefix_ != postfix_)
	{
		this->_status = Status::s_error | Status::s_is_writing;
		return this->index;
	}

	if (data_index != buffer_index)
	{
		this->_status = Status::s_error | Status::s_data_old;
		return this->index;
	}

	if (type == Read::next)
	{
		this->index = index_t + 1;
	}

	this->chronometry_end(start, this->min_read_time, this->max_read_time);

	return this->index;
}

/*
 * -----------------------------------------------
 * @ Read functor
 * -----------------------------------------------
 */

unsigned Shm::read(Read type, char *data, size_t size)
{
	return _read([&](unsigned current_buffer)
	{
		memcpy(data, this->shm_data(current_buffer), size);
	},
	type, size);
}

/*******************************************************************************************

										SHM STRUCT

*******************************************************************************************/

size_t Shm::shm_offset(size_t level)
{
	size_t offset = 0;

	switch (level)
	{
		case 7:
			offset += this->_buffer_size * this->_buffer_count; // Data
		[[fallthrough]];

		case  6:
			offset += sizeof(uint64_t) * this->_buffer_count; // Data (time)
		[[fallthrough]];

		case  5:
			offset += sizeof(shm_guard) * this->_buffer_count; // Data (guard)
		[[fallthrough]];

		case  4:
			offset += sizeof(std::atomic<unsigned>); // Write index
		[[fallthrough]];

		case  3:
			offset += sizeof(this->_buffer_count); // Buffer count
		[[fallthrough]];

		case  2:
			offset += sizeof(this->_buffer_size); // Buffer size
		[[fallthrough]];

		case  1:
			offset += sizeof(std::atomic<bool>) + 3; // Exist flag + x32 Align
		[[fallthrough]];

		default:;
	}

	return offset;
}

void Shm::shm_total_size()
{
	this->total_size = 0;                                                        // Init 0

	this->total_size += sizeof(std::atomic<bool>);                               // Exist flag
	this->total_size += 3;                                                       // x32 Align

	this->total_size += sizeof(size_t);                                          // Buffer size
	this->total_size += sizeof(size_t);                                          // Buffer count

	this->total_size += sizeof(std::atomic<unsigned>);                           // Write index

	this->total_size += sizeof(shm_guard) * this->_buffer_count;                 // Data (guard)
	this->total_size += sizeof(uint64_t) * this->_buffer_count;                  // Data (time)

	this->total_size += this->_buffer_size * this->_buffer_count;                // Data
}

std::atomic<bool> &Shm::shm_exist_flag()
{
	return *reinterpret_cast<std::atomic<bool> *>(&this->buffer[this->shm_offset(0)]);
}

size_t &Shm::shm_buffer_size()
{
	return *reinterpret_cast<size_t *>(&this->buffer[this->shm_offset(1)]);
}

size_t &Shm::shm_buffer_count()
{
	return *reinterpret_cast<size_t *>(&this->buffer[this->shm_offset(2)]);
}

std::atomic<unsigned> &Shm::shm_write_index()
{
	return *reinterpret_cast<std::atomic<unsigned> *>(&this->buffer[this->shm_offset(3)]);
}

Shm::shm_guard *Shm::shm_data_guard(size_t buffer_index)
{
	return reinterpret_cast<shm_guard *>(&this->buffer[sizeof(shm_guard) * buffer_index + this->shm_offset(4)]);
}

uint64_t &Shm::shm_data_time(size_t buffer_index)
{
	return *reinterpret_cast<uint64_t *>(&this->buffer[sizeof(uint64_t) * buffer_index + this->shm_offset(5)]);
}

char *Shm::shm_data(size_t buffer_index)
{
	return &this->buffer[this->_buffer_size * buffer_index + this->shm_offset(6)];
}

/*******************************************************************************************

										SHM STRUCT

*******************************************************************************************/

void Shm::read_reset()
{
	this->index = 0;
}

high_resolution_clock::time_point Shm::chronometry_start()
{
	high_resolution_clock::time_point start;

	if (this->chronometry)
	{
		start = high_resolution_clock::now();
	}

	return start;
}

void Shm::chronometry_end(high_resolution_clock::time_point start, unsigned &min, unsigned &max)
{
	if (this->chronometry)
	{
		auto end = high_resolution_clock::now();

		unsigned exe = duration_cast<nanoseconds>(end - start).count();

		if (exe < min)
		{
			min = exe;
		}

		if (exe > max)
		{
			max = exe;
		}
	}
}
