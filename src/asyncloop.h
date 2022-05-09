/*
Minetest
Copyright (C) 2022 Minetest Authors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#pragma once

#include <cstdlib>
#include <vector>
#include <functional>
#include <queue>

class AsyncLoop {
public:
	using VCallback = std::function<void()>;

	void add_callback(const VCallback &callback) {
		m_pending.push(callback);
	}

	void add_callback(VCallback &&callback) {
		m_pending.emplace(std::move(callback));
	}

	void atexit(const VCallback &callback) {
		m_atexits.push_back(callback);
	}

	void atexit(VCallback &&callback) {
		m_atexits.emplace_back(std::move(callback));
	}

	// Schedule exit with a given status
	void exit(int status) {
		m_running = false;
		exit_status = status;
	}

	void loop() {
		while (m_running) {
			m_pending.swap(m_active);
			while (!m_active.empty()) {
				VCallback cb = std::move(m_active.front());
				m_active.pop();
				cb();
			}
			if (m_pending.empty())
				break;
		}
		clean_exit();
	}

private:
	void clean_exit() {
		while (!m_atexits.empty()) {
			VCallback cb = std::move(m_atexits.back());
			m_atexits.pop_back();
			cb();
		}
		std::exit(exit_status);
	}
	bool m_running = true;
	int exit_status = 0;
	std::vector<VCallback> m_atexits; // First In, Last Out
	std::queue<VCallback> m_pending; // First In, First Out
	std::queue<VCallback> m_active;
};

extern AsyncLoop g_mainloop;
