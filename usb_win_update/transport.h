#pragma once
#pragma once
/*
* Copyright (C) 2015 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef TRANSPORT_H_
#define TRANSPORT_H_

// A macro to disallow the copy constructor and operator= functions
// This must be placed in the private: declarations for a class.
//
// For disallowing only assign or copy, delete the relevant operator or
// constructor, for example:
// void operator=(const TypeName&) = delete;
// Note, that most uses of DISALLOW_ASSIGN and DISALLOW_COPY are broken
// semantically, one should either use disallow both or neither. Try to
// avoid these in new code.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

// General interface to allow the fastboot protocol to be used over different
// types of transports.
class Transport {
public:
	Transport() = default;
	virtual ~Transport() = default;

	// Reads |len| bytes into |data|. Returns the number of bytes actually
	// read or -1 on error.
	virtual ssize_t Read(void* data, size_t len) = 0;

	// Writes |len| bytes from |data|. Returns the number of bytes actually
	// written or -1 on error.
	virtual ssize_t Write(const void* data, size_t len) = 0;

	// Reads or Writes |len| bytes from/to data. Returns the number of bytes actually
	// read or written or -1 on error
	virtual ssize_t ControlIO(bool is_in, void *setup, void* data, size_t len) = 0;

	// Closes the underlying transport. Returns 0 on success.
	virtual int Close() = 0;

	// Blocks until the transport disconnects. Transports that don't support
	// this will return immediately. Returns 0 on success.
	virtual int WaitForDisconnect() { return 0; }
};

#endif  // TRANSPORT_H_
