// c++ -- http://code.google.com/p/c++
// Copyright (c) 2010 Kenton Varda and contributors.  All rights reserved.
// Portions copyright Google, Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of the c++ project nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
