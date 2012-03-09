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

#include "Table.h"
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <map>
#include <string>

namespace ekam {
namespace {

#define ASSERT(EXPRESSION)                                                    \
  if (!(EXPRESSION)) {                                                        \
    fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #EXPRESSION);  \
    exit(1);                                                                  \
  }

void testTable() {
  {
    typedef Table<IndexedColumn<int> > MyTable;
    MyTable table;

    table.add(1234);
    table.add(5678);

    const MyTable::Row* row = table.find<0>(1234);
    ASSERT(table.find<0>(4321) == NULL);
    ASSERT(row != NULL);
    ASSERT(row->cell<0>() == 1234);
    row = table.find<0>(5678);
    ASSERT(row != NULL);
    ASSERT(row->cell<0>() == 5678);

    {
      std::multiset<int> values;
      MyTable::RowIterator iter(table);

      ASSERT(iter.next());
      values.insert(iter.cell<0>());
      ASSERT(iter.next());
      values.insert(iter.cell<0>());
      ASSERT(!iter.next());

      ASSERT(values.count(1234) == 1);
      ASSERT(values.count(5678) == 1);
    }

    table.erase<0>(1234);

    ASSERT(table.find<0>(4321) == NULL);
    ASSERT(table.find<0>(1234) == NULL);
    row = table.find<0>(5678);
    ASSERT(row != NULL);
    ASSERT(row->cell<0>() == 5678);
  }

  {
    typedef Table<UniqueColumn<int>, IndexedColumn<int> > MyTable;
    MyTable table;

    table.add(12, 34);
    table.add(56, 34);

    {
      std::multiset<int> values;
      MyTable::SearchIterator<1> iter(table, 34);

      ASSERT(iter.next());
      values.insert(iter.cell<0>());
      ASSERT(iter.next());
      values.insert(iter.cell<0>());
      ASSERT(!iter.next());

      ASSERT(values.count(12) == 1);
      ASSERT(values.count(56) == 1);
    }

    table.add(12, 78);
    const MyTable::Row* row = table.find<0>(12);
    ASSERT(row != NULL);
    ASSERT(row->cell<0>() == 12);
    ASSERT(row->cell<1>() == 78);

    {
      MyTable::SearchIterator<1> iter(table, 34);

      ASSERT(iter.next());
      ASSERT(iter.cell<0>() == 56);
      ASSERT(!iter.next());
    }
  }

  {
    typedef Table<IndexedColumn<std::string>, IndexedColumn<int>, Column<char> > MyTable;
    MyTable table;

    table.add("foo", 1, 'f');
    table.add("foo", 2, 'o');
    table.add("foo", 3, 'o');
    table.add("bar", 1, 'b');
    table.add("bar", 2, 'a');
    table.add("bar", 3, 'r');

    {
      std::map<int, char> values;
      MyTable::SearchIterator<0> iter(table, "bar");

      ASSERT(iter.next());
      ASSERT(iter.cell<0>() == "bar");
      values[iter.cell<1>()] = iter.cell<2>();
      ASSERT(iter.next());
      ASSERT(iter.cell<0>() == "bar");
      values[iter.cell<1>()] = iter.cell<2>();
      ASSERT(iter.next());
      ASSERT(iter.cell<0>() == "bar");
      values[iter.cell<1>()] = iter.cell<2>();
      ASSERT(!iter.next());

      ASSERT(values[1] == 'b');
      ASSERT(values[2] == 'a');
      ASSERT(values[3] == 'r');
    }

    {
      std::map<std::string, char> values;
      MyTable::SearchIterator<1> iter(table, 2);

      ASSERT(iter.next());
      ASSERT(iter.cell<1>() == 2);
      values[iter.cell<0>()] = iter.cell<2>();
      ASSERT(iter.next());
      ASSERT(iter.cell<1>() == 2);
      values[iter.cell<0>()] = iter.cell<2>();
      ASSERT(!iter.next());

      ASSERT(values["foo"] == 'o');
      ASSERT(values["bar"] == 'a');
    }
  }

  {
    typedef Table<IndexedColumn<int>, IndexedColumn<int> > MyTable;
    MyTable table;

    for (int i = 0; i < 50; i++) {
      table.add(123, i);
      table.add(456, i);
      table.add(789, i);
    }

    ASSERT(table.size() == 150);
    ASSERT(table.capacity() >= 150);
    ASSERT(table.indexSize<0>() == 150);
    ASSERT(table.indexSize<1>() == 150);
    table.erase<0>(123);

    ASSERT(table.size() == 100);
    ASSERT(table.capacity() >= 150);
    ASSERT(table.indexSize<0>() == 100);
    ASSERT(table.indexSize<1>() == 150);
    table.erase<0>(456);

    ASSERT(table.size() == 50);
    ASSERT(table.capacity() == 50);
    ASSERT(table.indexSize<0>() == 50);
    ASSERT(table.indexSize<1>() == 50);

    const MyTable::Row* row = table.find<1>(5);
    ASSERT(row != NULL);
    ASSERT(row->cell<0>() == 789);

    {
      std::multiset<int> values;
      MyTable::SearchIterator<0> iter(table, 789);

      for (int i = 0; i < 50; i++) {
        ASSERT(iter.next());
        values.insert(iter.cell<1>());
      }
      ASSERT(!iter.next());

      for (int i = 0; i < 50; i++) {
        ASSERT(values.count(i) == 1);
      }
    }

    ASSERT(!MyTable::SearchIterator<0>(table, 123).next());
    ASSERT(!MyTable::SearchIterator<0>(table, 456).next());

    {
      std::multiset<int> values0;
      std::multiset<int> values1;
      MyTable::RowIterator iter(table);

      for (int i = 0; i < 50; i++) {
        ASSERT(iter.next());
        values0.insert(iter.cell<0>());
        values1.insert(iter.cell<1>());
      }
      ASSERT(!iter.next());

      ASSERT(values0.count(789) == 50);
      for (int i = 0; i < 50; i++) {
        ASSERT(values1.count(i) == 1);
      }
    }
  }
}

}  // namespace
}  // namespace ekam

int main(int argc, char* argv[]) {
  ekam::testTable();
  return 0;
}
