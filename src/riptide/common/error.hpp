#pragma once

#include <stdexcept>
#include <string>

namespace riptide {

/// Raised when a DEFLATE stream is malformed or truncated.
class DecodeError : public std::runtime_error {
public:
    explicit DecodeError(const std::string& message) : std::runtime_error(message) {}
};

/// Raised when the gzip container around a DEFLATE stream is malformed.
class GzipFormatError : public DecodeError {
public:
    explicit GzipFormatError(const std::string& message) : DecodeError(message) {}
};

/// Raised when decoded data does not match the checksum or size in the gzip footer.
class ChecksumError : public DecodeError {
public:
    explicit ChecksumError(const std::string& message) : DecodeError(message) {}
};

}  // namespace riptide
