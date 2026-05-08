#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace tts::json {

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Value {
public:
    using Object = std::map<std::string, Value>;
    using Array  = std::vector<Value>;

    enum class Kind { Null, Bool, Int, Double, String, Array_, Object_ };

    Value() = default;
    Value(std::nullptr_t)      : kind_(Kind::Null) {}
    Value(bool b)              : kind_(Kind::Bool),    bool_(b) {}
    Value(int v)               : kind_(Kind::Int),     int_(v)  {}
    Value(long v)              : kind_(Kind::Int),     int_(v)  {}
    Value(long long v)         : kind_(Kind::Int),     int_(v)  {}
    Value(double v)            : kind_(Kind::Double),  double_(v) {}
    Value(const char* s)       : kind_(Kind::String),  str_(s)  {}
    Value(std::string s)       : kind_(Kind::String),  str_(std::move(s)) {}
    Value(Array a)             : kind_(Kind::Array_),  arr_(std::move(a)) {}
    Value(Object o)            : kind_(Kind::Object_), obj_(std::move(o)) {}

    Kind kind()    const { return kind_; }
    bool isNull()   const { return kind_ == Kind::Null; }
    bool isBool()   const { return kind_ == Kind::Bool; }
    bool isInt()    const { return kind_ == Kind::Int; }
    bool isDouble() const { return kind_ == Kind::Double; }
    bool isNumber() const { return kind_ == Kind::Int || kind_ == Kind::Double; }
    bool isString() const { return kind_ == Kind::String; }
    bool isArray()  const { return kind_ == Kind::Array_; }
    bool isObject() const { return kind_ == Kind::Object_; }

    bool                   toBool()   const;
    long long              toInt()    const;
    double                 toDouble() const;
    const std::string&     toString() const;
    const Array&           toArray()  const;
    const Object&          toObject() const;

    const Value* find(const std::string& key) const;

    static Value parse(const std::string& src);
    std::string  dump() const;

private:
    Kind        kind_ = Kind::Null;
    bool        bool_ = false;
    long long   int_  = 0;
    double      double_ = 0.0;
    std::string str_;
    Array       arr_;
    Object      obj_;
};

}  // namespace tts::json
