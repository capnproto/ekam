// Kenton's Code Playground -- http://code.google.com/p/kentons-code
// Author: Kenton Varda (temporal@gmail.com)
// Copyright (c) 2010 Google, Inc. and contributors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef KENTONSCODE_BASE_TABLE_H_
#define KENTONSCODE_BASE_TABLE_H_

#include <tr1/unordered_map>
#include <vector>
#include <stdlib.h>

namespace ekam {

// =======================================================================================
// Internal helpers.  Please ignore.

template <typename Key, typename Value>
class DummyMap {
public:
  typedef std::pair<const Key, Value> value_type;
  typedef value_type* iterator;
  typedef const value_type* const_iterator;
  inline iterator begin() { return NULL; }
  inline iterator end() { return NULL; }
  inline const_iterator begin() const { return NULL; }
  inline const_iterator end() const { return NULL; }
  inline iterator insert(const value_type& value) {
    return NULL;
  }
  inline int erase(const iterator& iter) {
    return 0;
  }
};

template <typename Choices, int index>
struct ChooseType;
template <typename Choices>
struct ChooseType<Choices, 0> : public Choices::Choice0 {};
template <typename Choices>
struct ChooseType<Choices, 1> : public Choices::Choice1 {};
template <typename Choices>
struct ChooseType<Choices, 2> : public Choices::Choice2 {};

// =======================================================================================

template <typename T, typename Hasher = std::tr1::hash<T>, typename Eq = std::equal_to<T> >
struct IndexedColumn {
  typedef T Value;
  typedef std::tr1::unordered_multimap<T, int, Hasher, Eq> Index;
};

template <typename T, typename Hasher = std::tr1::hash<T>, typename Eq = std::equal_to<T> >
struct UniqueColumn {
  typedef T Value;
  typedef std::tr1::unordered_map<T, int, Hasher, Eq> Index;
};

template <typename T, typename Hasher = std::tr1::hash<T>, typename Eq = std::equal_to<T> >
struct Column {
  typedef T Value;
  typedef DummyMap<T, int> Index;
};

struct EmptyColumn {
  struct Value {};
  typedef DummyMap<Value, int> Index;
};

template <typename Column0, typename Column1 = EmptyColumn, typename Column2 = EmptyColumn>
class Table {
private:
  struct Columns {
#define CHOICE(INDEX) \
    struct Choice##INDEX : public Column##INDEX { \
      static inline typename Column##INDEX::Index* index(Table* table) { \
        return &table->index##INDEX; \
      } \
      static inline const typename Column##INDEX::Index& index(const Table& table) { \
        return table.index##INDEX; \
      } \
      template <typename Row> \
      static inline const typename Column##INDEX::Value& cell(const Row& row) { \
        return row.cell##INDEX; \
      } \
    };
    CHOICE(0)
    CHOICE(1)
    CHOICE(2)
#undef CHOICE
  };
  template <int columnNumber>
  class Column : public ChooseType<Columns, columnNumber> {};

public:
  Table() : deletedCount(0) {}
  ~Table() {}

  class Row {
  public:
    inline Row() {}

    template <int columnNumber>
    inline const typename Column<columnNumber>::Value& cell() const {
      return Column<columnNumber>::cell(*this);
    }

  private:
    typename Column<0>::Value cell0;
    typename Column<1>::Value cell1;
    typename Column<2>::Value cell2;
    bool deleted;

    inline Row(const typename Column<0>::Value& cell0,
               const typename Column<1>::Value& cell1,
               const typename Column<2>::Value& cell2)
      : cell0(cell0), cell1(cell1), cell2(cell2), deleted(false) {}

    friend class Table;
  };

  class RowIterator {
  public:
    inline RowIterator() {}
    RowIterator(const Table& table)
        : current(NULL), nextIter(table.rows.begin()), end(table.rows.end()) {}

    bool next() {
      while (true) {
        if (nextIter == end) {
          return false;
        } else if (!nextIter->deleted) {
          current = &*nextIter;
          ++nextIter;
          return true;
        }
        ++nextIter;
      }
    }

    template <int columnNumber>
    inline const typename Column<columnNumber>::Value& cell() const {
      return Column<columnNumber>::cell(*current);
    }

  private:
    const Row* current;
    typename std::vector<Row>::const_iterator nextIter;
    const typename std::vector<Row>::const_iterator end;
  };

  template <int columnNumber>
  class SearchIterator {
  public:
    inline SearchIterator() {}
    SearchIterator(const Table& table, const typename Column<columnNumber>::Value& value)
        : table(table), current(NULL),
          range(Column<columnNumber>::index(table).equal_range(value)) {}

    inline bool next() {
      while (true) {
        if (range.first == range.second) {
          return false;
        }

        const Row* row = &table.rows[range.first->second];

        if (!row->deleted) {
          current = row;
          ++range.first;
          return true;
        }

        ++range.first;
      }
    }

    template <int cellColumnNumber>
    inline const typename Column<cellColumnNumber>::Value& cell() const {
      return Column<cellColumnNumber>::cell(*current);
    }

  private:
    typedef typename Column<columnNumber>::Index::const_iterator InnerIter;
    const Table& table;
    const Row* current;
    std::pair<InnerIter, InnerIter> range;
  };

  template <int columnNumber>
  const Row* find(const typename Column<columnNumber>::Value& value) const {
    typename Column<columnNumber>::Index::const_iterator iter =
        Column<columnNumber>::index(*this).find(value);
    if (iter == Column<columnNumber>::index(*this).end()) {
      return NULL;
    } else {
      const Row* row = &rows[iter->second];
      return row->deleted ? NULL : row;
    }
  }

  template <int columnNumber>
  const size_t erase(const typename Column<columnNumber>::Value& value) {
    typedef typename Column<columnNumber>::Index::iterator ColumnIterator;
    std::pair<ColumnIterator, ColumnIterator> range =
        Column<columnNumber>::index(this)->equal_range(value);

    size_t count = 0;
    for (ColumnIterator iter = range.first; iter != range.second; ++iter) {
      if (!rows[iter->second].deleted) {
        rows[iter->second].deleted = true;
        ++count;
      }
    }

    deletedCount += count;
    if (deletedCount >= 16 && deletedCount > rows.size() / 2) {
      refresh();
    } else {
      // This isn't strictly necessary, but since it's convenient, let's delete the range
      // from the index.
      Column<columnNumber>::index(this)->erase(range.first, range.second);
    }

    return count;
  }

  void add(const typename Column<0>::Value& value0,
           const typename Column<1>::Value& value1 = EmptyColumn::Value(),
           const typename Column<2>::Value& value2 = EmptyColumn::Value()) {
    handleInsertResult(index0.insert(typename Column<0>::Index::value_type(value0, rows.size())));
    handleInsertResult(index1.insert(typename Column<1>::Index::value_type(value1, rows.size())));
    handleInsertResult(index2.insert(typename Column<2>::Index::value_type(value2, rows.size())));
    rows.push_back(Row(value0, value1, value2));
  }

  template <int columnNumber>
  const bool has(const typename Column<columnNumber>::Value& value) {
    typedef typename Column<columnNumber>::Index::iterator ColumnIterator;
    std::pair<ColumnIterator, ColumnIterator> range =
        Column<columnNumber>::index(this)->equal_range(value);

    for (ColumnIterator iter = range.first; iter != range.second; ++iter) {
      if (!rows[iter->second].deleted) {
        return true;
      }
    }
    return false;
  }

  int size() {
    return rows.size() - deletedCount;
  }
  int capacity() {
    return rows.capacity();
  }

  template <int columnNumber>
  int indexSize() {
    return Column<columnNumber>::index(this)->size();
  }

private:
  std::vector<Row> rows;
  size_t deletedCount;

  typename Column<0>::Index index0;
  typename Column<1>::Index index1;
  typename Column<2>::Index index2;

  void refresh() {
    std::vector<int> newLocations;
    newLocations.reserve(rows.size());

    {
      std::vector<Row> newRows;
      newRows.reserve(rows.size() - deletedCount);

      for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i].deleted) {
          newLocations.push_back(-1);
        } else {
          newLocations.push_back(newRows.size());
          newRows.push_back(rows[i]);
        }
      }

      rows.swap(newRows);
      deletedCount = 0;
    }

    refreshColumn<0>(newLocations);
    refreshColumn<1>(newLocations);
    refreshColumn<2>(newLocations);
  }

  template <int columnNumber>
  void refreshColumn(const std::vector<int>& newLocations) {
    typedef Column<columnNumber> C;
    typedef typename C::Index::iterator ColumnIterator;
    ColumnIterator iter = C::index(this)->begin();
    while (iter != C::index(this)->end()) {
      int newLocation = newLocations[iter->second];
      if (newLocation == -1) {
        ColumnIterator iter2 = iter;
        ++iter2;
        C::index(this)->erase(iter);
        iter = iter2;
      } else {
        iter->second = newLocation;
        ++iter;
      }
    }
  }

  template <typename Iterator>
  void handleInsertResult(const std::pair<Iterator, bool>& result) {
    if (!result.second) {
      rows[result.first->second].deleted = true;
      result.first->second = rows.size();
    }
  }

  template <typename Iterator>
  void handleInsertResult(const Iterator& iter) {}
};

}  // namespace ekam

#endif  // KENTONSCODE_BASE_TABLE_H_
