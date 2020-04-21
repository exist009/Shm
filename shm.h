/*
 * -----------------------------------------------
 * © Egor Boltov (exist009)
 * -----------------------------------------------
 */

#ifndef __shm_h__
#define __shm_h__

#include <limits.h>
#include <stddef.h>
#include <stdbool.h>

#include <chrono>
#include <atomic>
#include <vector>

static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "atomic_bool must be lock-free");
static_assert(ATOMIC_CHAR_LOCK_FREE == 2, "atomic_char must be lock-free");
static_assert(ATOMIC_INT_LOCK_FREE  == 2, "atomic_int must be lock-free");

#define SHM_MAX_NAME (NAME_MAX - 1 - 1)

class Shm
{
	public:

		enum Status : int
		{
			s_ok           = 1 << 0, // Ок
			s_error        = 1 << 1, // Ошибка
			s_no_data      = 1 << 2, // Нет новых данных
			s_data_loss    = 1 << 3, // Потеря данных
			s_data_old     = 1 << 4, // Устаревшие данные
			s_is_writing   = 1 << 5, // Данные не записаны
			s_range_out    = 1 << 6, // Размер не соответствует размеру буффер
			s_not_exist    = 1 << 7, // Разделяемая память удалена
		};

		enum Option : int
		{
			o_create         = 1 << 0,
			o_open           = 1 << 1,
			o_create_or_open = 1 << 2,
		};

		enum Mode : int
		{
			m_read  = 1 << 0,
			m_write = 1 << 1,
		};

		enum class Read { next, last };

		static const size_t max_name = SHM_MAX_NAME;

		/*
		 * Конструктор Shm предназначен для инициализации разделяемой памяти
		 * [input] option - Опция инициализирования:
		 * - только создание
		 * - только открытие
		 * - создание или открытие
		 * [input] name - Идентификатор (имя) области памяти
		 * [input] mode - Режим доступа:
		 * - чтение
		 * - запись
		 * - чтение и запись
		 * [input] buffer_size - Размер буффера данных
		 * [input] buffer_count - Количество буфферов данных
		 */
		Shm(int option, const char *name, int mode = Mode::m_read | Mode::m_write, size_t buffer_size = 0, size_t buffer_count = 0);



		/*
		 * Деструктор ~Shm предназначен для освобождения занятых ресурсов после удаления объекта
		 */
		~Shm() { this->close(); }



		/*
		 * Статическая функция remove предназначена для удаления заданной разделяемой памяти
		 * [input] name - Идентификатор (имя) области памяти
		 */
		static int remove(const char *name);



		/*
		 * Статическая функция check предназначена для проверки существования заданной разделяемой памяти
		 * [input] name - Идентификатор (имя) области памяти
		 */
		static bool check(const char *name);



		/*
		 * Функция get_status предназначена для получения статуса выполнения последней функции из набора: (constructor, read, write)
		 */
		int get_status() { return this->_status; }



		/*
		 * Функция get_last_read_time предназначена для получения времени записи последнего прочитанного буфера данных (мкс)
		 */
		uint64_t get_last_read_time() { return this->last_read_time; }



		/*
		 * Функция get_total_size предназначена для получения полного размера разделяемой памяти
		 */
		size_t get_total_size() { return this->total_size; }



		/*
		 * Функция get_buffer_size предназначена для получения размера одного буффера данных
		 */
		size_t get_buffer_size() { return this->_buffer_size; }



		/*
		 * Функция get_buffer_count предназначена для получения количества буфферов данных
		 */
		size_t get_buffer_count() { return this->_buffer_count; }



		/*
		 * Функция close предназначена для освобождения занятых ресурсов
		 */
		int close();



		/*
		 * Функция remove предназначена для удаления разделяемой памяти с установкой байта существования в "false"
		 */
		int remove();



		/*
		 * Функция check предназначена для проверки существования разделяемой памяти
		 */
		bool check();



		/*
		 * Функция write предназначена для записи данных в буффер разделяемой памяти
		 * [input] data Записываемые данные
		 * [input] size Размер записываемых данных
		 */
		void write(const char *data, size_t size);



		/*
		 * Функция read предназначена для чтения данных из последнего/следующего непрочтенного буффера разделяемой памяти
		 * [input] type Последний/Следующий буффер
		 * [output] data Буффер для получения данных
		 * [input] size Размер передаваемого буффера
		 */
		unsigned read(Read type, char *data, size_t size);



		/*
		 * @ Функция read_reset позволяет выполнить повторное чтение последнего буффера разделяемой памяти
		 */
		void read_reset();



		/*
		 * @ Функция set_chronometry разрешает/запрещает измерение времени выполнения (min & max) функций: write, read
		 * @ Default: false
		 * [input] state - Разрешить (true), запретить (false)
		 */
		void set_chronometry(bool state) { this->chronometry = state; }

		unsigned get_min_write_time() { return this->min_write_time; }
		unsigned get_max_write_time() { return this->max_write_time; }

		unsigned get_min_read_time() { return this->min_read_time; }
		unsigned get_max_read_time() { return this->max_read_time; }

	private:

		Shm(const Shm &);
		Shm &operator=(const Shm &);

		typedef struct
		{
			std::atomic<char> prefix;
			std::atomic<char> postfix;
			std::atomic<unsigned> index;
		} shm_guard;

		char *buffer = nullptr;

		int _status = Status::s_error;
		int _mode = Mode::m_read | Mode::m_write;

		char _name[NAME_MAX];

		size_t _buffer_size = 0;
		size_t _buffer_count = 0;

		size_t total_size = 0;

		unsigned index = 0;

		int file_d = -1;

		uint64_t last_read_time = 0;

		bool chronometry = false;

		unsigned min_write_time = UINT_MAX;
		unsigned max_write_time = 0;

		unsigned min_read_time = UINT_MAX;
		unsigned max_read_time = 0;

		void *address = nullptr;

		int create();
		int open();
		int memory_init(bool);

		size_t inline shm_offset(size_t);
		void inline shm_total_size();

		int &shm_key();
		std::atomic<bool> &shm_exist_flag();
		size_t &shm_buffer_size();
		size_t &shm_buffer_count();

		std::atomic<unsigned> &shm_write_index();

		shm_guard *shm_data_guard(size_t);

		uint64_t &shm_data_time(size_t);

		char *shm_data(size_t);

		std::chrono::high_resolution_clock::time_point chronometry_start();
		void chronometry_end(std::chrono::high_resolution_clock::time_point start, unsigned &min, unsigned &max);

		int _write_start(size_t &, char &);
		void _write_end(size_t, char);

		template<typename F>
		unsigned _read(F read_functor, Read type, size_t &size);

};

#endif //__shm_h__
