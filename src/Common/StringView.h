// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cassert>
#include <string>

namespace Common {

// 'StringView' is a pair of pointer to constant char and size.
// It is recommended to pass 'StringView' to procedures by value.
// 'StringView' supports 'EMPTY' and 'NIL' representations as follows:
//   'data' == 'nullptr' && 'size' == 0 - EMPTY NIL
//   'data' != 'nullptr' && 'size' == 0 - EMPTY NOTNIL
//   'data' == 'nullptr' && 'size' > 0 - Undefined
//   'data' != 'nullptr' && 'size' > 0 - NOTEMPTY NOTNIL
class StringView {
public:
  typedef char Object;
  typedef size_t Size;

  const static Size INVALID;
  const static StringView EMPTY;
  const static StringView NIL;

  // Default constructor.
  // Leaves object uninitialized. Any usage before initializing it is undefined.
  StringView();

  // Direct constructor.
  // The behavior is undefined unless 'stringData' != 'nullptr' || 'stringSize' == 0
  StringView(const Object* stringData, Size stringSize);

  // Constructor from C array.
  // The behavior is undefined unless 'stringData' != 'nullptr' || 'stringSize' == 0. Input state can be malformed using poiner conversions.
  template<Size stringSize> StringView(const Object(&stringData)[stringSize]) : data(stringData), size(stringSize - 1) {
    assert(data != nullptr || size == 0);
  }

  // Constructor from std::string
  StringView(const std::string& string);

  // Copy constructor.
  // Performs default action - bitwise copying of source object.
  // The behavior is undefined unless 'other' 'StringView' is in defined state, that is 'data' != 'nullptr' || 'size' == 0
  StringView(const StringView& other);

  // Destructor.
  // No special action is performed.
  ~StringView();

  // Copy assignment operator.
  // The behavior is undefined unless 'other' 'StringView' is in defined state, that is 'data' != 'nullptr' || 'size' == 0
  StringView& operator=(const StringView& other);

  explicit operator std::string() const;

  const Object* getData() const;

  Size getSize() const;

  // Return false if 'StringView' is not EMPTY.
  // The behavior is undefined unless 'StringView' was initialized.
  bool isEmpty() const;

  // Return false if 'StringView' is not NIL.
  // The behavior is undefined unless 'StringView' was initialized.
  bool isNil() const;

  // Get 'StringView' element by index.
  // The behavior is undefined unless 'StringView' was initialized and 'index' < 'size'.
  const Object& operator[](Size index) const;

  // Get first element.
  // The behavior is undefined unless 'StringView' was initialized and 'size' > 0
  const Object& first() const;

  // Get last element.
  // The behavior is undefined unless 'StringView' was initialized and 'size' > 0
  const Object& last() const;

  // Return a pointer to the first element.
  // The behavior is undefined unless 'StringView' was initialized.
  const Object* begin() const;

  // Return a pointer after the last element.
  // The behavior is undefined unless 'StringView' was initialized.
  const Object* end() const;

  // Compare elements of two strings, return false if there is a difference.
  // EMPTY and NIL strings are considered equal.
  // The behavior is undefined unless both strings were initialized.
  bool operator==(StringView other) const;

  // Compare elements two strings, return false if there is no difference.
  // EMPTY and NIL strings are considered equal.
  // The behavior is undefined unless both strings were initialized.
  bool operator!=(StringView other) const;

  // Compare two strings character-wise.
  // The behavior is undefined unless both strings were initialized.
  bool operator<(StringView other) const;

  // Compare two strings character-wise.
  // The behavior is undefined unless both strings were initialized.
  bool operator<=(StringView other) const;

  // Compare two strings character-wise.
  // The behavior is undefined unless both strings were initialized.
  bool operator>(StringView other) const;

  // Compare two strings character-wise.
  // The behavior is undefined unless both strings were initialized.
  bool operator>=(StringView other) const;

  // Return false if 'StringView' does not contain 'object' at the beginning.
  // The behavior is undefined unless 'StringView' was initialized.
  bool beginsWith(const Object& object) const;

  // Return false if 'StringView' does not contain 'other' at the beginning.
  // The behavior is undefined unless both strings were initialized.
  bool beginsWith(StringView other) const;

  // Return false if 'StringView' does not contain 'object'.
  // The behavior is undefined unless 'StringView' was initialized.
  bool contains(const Object& object) const;

  // Return false if 'StringView' does not contain 'other'.
  // The behavior is undefined unless both strings were initialized.
  bool contains(StringView other) const;

  // Return false if 'StringView' does not contain 'object' at the end.
  // The behavior is undefined unless 'StringView' was initialized.
  bool endsWith(const Object& object) const;

  // Return false if 'StringView' does not contain 'other' at the end.
  // The behavior is undefined unless both strings were initialized.
  bool endsWith(StringView other) const;

  // Looks for the first occurence of 'object' in 'StringView',
  // returns index or INVALID if there are no occurences.
  // The behavior is undefined unless 'StringView' was initialized.
  Size find(const Object& object) const;

  // Looks for the first occurence of 'other' in 'StringView',
  // returns index or INVALID if there are no occurences.
  // The behavior is undefined unless both strings were initialized.
  Size find(StringView other) const;

  // Looks for the last occurence of 'object' in 'StringView',
  // returns index or INVALID if there are no occurences.
  // The behavior is undefined unless 'StringView' was initialized.
  Size findLast(const Object& object) const;

  // Looks for the first occurence of 'other' in 'StringView',
  // returns index or INVALID if there are no occurences.
  // The behavior is undefined unless both strings were initialized.
  Size findLast(StringView other) const;

  // Returns substring of 'headSize' first elements.
  // The behavior is undefined unless 'StringView' was initialized and 'headSize' <= 'size'.
  StringView head(Size headSize) const;

  // Returns substring of 'tailSize' last elements.
  // The behavior is undefined unless 'StringView' was initialized and 'tailSize' <= 'size'.
  StringView tail(Size tailSize) const;

  // Returns 'StringView' without 'headSize' first elements.
  // The behavior is undefined unless 'StringView' was initialized and 'headSize' <= 'size'.
  StringView unhead(Size headSize) const;

  // Returns 'StringView' without 'tailSize' last elements.
  // The behavior is undefined unless 'StringView' was initialized and 'tailSize' <= 'size'.
  StringView untail(Size tailSize) const;

  // Returns substring starting at 'startIndex' and contaning 'endIndex' - 'startIndex' elements.
  // The behavior is undefined unless 'StringView' was initialized and 'startIndex' <= 'endIndex' and 'endIndex' <= 'size'.
  StringView range(Size startIndex, Size endIndex) const;

  // Returns substring starting at 'startIndex' and contaning 'sliceSize' elements.
  // The behavior is undefined unless 'StringView' was initialized and 'startIndex' <= 'size' and 'startIndex' + 'sliceSize' <= 'size'.
  StringView slice(Size startIndex, Size sliceSize) const;

protected:
  const Object* data;
  Size size;
};

}
