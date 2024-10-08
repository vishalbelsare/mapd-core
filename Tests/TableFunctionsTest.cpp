/*
 * Copyright 2022 HEAVY.AI, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "TestHelpers.h"

#include <gtest/gtest.h>

#include "../Shared/DateTimeParser.h"

#include "QueryEngine/ResultSet.h"
#include "QueryEngine/TableFunctions/TableFunctionManager.h"
#include "QueryRunner/QueryRunner.h"

#ifndef BASE_PATH
#define BASE_PATH "./tmp"
#endif

using QR = QueryRunner::QueryRunner;

extern bool g_enable_table_functions;
extern bool g_enable_dev_table_functions;
namespace {

inline void run_ddl_statement(const std::string& stmt) {
  QR::get()->runDDLStatement(stmt);
}

std::shared_ptr<ResultSet> run_multiple_agg(const std::string& query_str,
                                            const ExecutorDeviceType device_type) {
  // Following line left but commented out for easy debugging
  // std::cout << std::endl << "Query: " << query_str << std::endl;
  return QR::get()->runSQL(query_str, device_type, false, false);
}

}  // namespace

bool skip_tests(const ExecutorDeviceType device_type) {
#ifdef HAVE_CUDA
  return device_type == ExecutorDeviceType::GPU && !(QR::get()->gpusPresent());
#else
  return device_type == ExecutorDeviceType::GPU;
#endif
}

#define SKIP_NO_GPU()                                        \
  if (skip_tests(dt)) {                                      \
    CHECK(dt == ExecutorDeviceType::GPU);                    \
    LOG(WARNING) << "GPU not available, skipping GPU tests"; \
    continue;                                                \
  }

class TableFunctions : public ::testing::Test {
  void SetUp() override {
    {
      run_ddl_statement("DROP TABLE IF EXISTS tf_test;");
      run_ddl_statement(
          "CREATE TABLE tf_test (x INT, x2 INT, f FLOAT, d "
          "DOUBLE, d2 DOUBLE) WITH "
          "(FRAGMENT_SIZE=2);");

      TestHelpers::ValuesGenerator gen("tf_test");

      for (int i = 0; i < 5; i++) {
        const auto insert_query = gen(i, 5 - i, i * 1.1, i * 1.1, 1.0 - i * 2.2);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS tf_test2;");
      run_ddl_statement(
          "CREATE TABLE tf_test2 (x2 INT, d2 INT) WITH "
          "(FRAGMENT_SIZE=2);");

      TestHelpers::ValuesGenerator gen("tf_test2");

      for (int i = 0; i < 5; i++) {
        const auto insert_query = gen(i, i * i);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS sd_test;");
      run_ddl_statement(
          "CREATE TABLE sd_test ("
          "   base TEXT ENCODING DICT(32),"
          "   derived TEXT,"
          "   t1 TEXT ENCODING DICT(32),"
          "   t2 TEXT,"
          "   t3 TEXT ENCODING DICT(32),"
          "   SHARED DICTIONARY (derived) REFERENCES sd_test(base),"
          "   SHARED DICTIONARY (t2) REFERENCES sd_test(t1)"
          ");");

      TestHelpers::ValuesGenerator gen("sd_test");
      std::vector<std::vector<std::string>> v = {
          {"'hello'", "'world'", "'California'", "'California'", "'California'"},
          {"'foo'", "'bar'", "'Ohio'", "'Ohio'", "'North Carolina'"},
          {"'bar'", "'baz'", "'New York'", "'Indiana'", "'Indiana'"},
          {"'world'", "'foo'", "'New York'", "'New York'", "'New York'"},
          {"'baz'", "'hello'", "'New York'", "'Ohio'", "'California'"}};

      for (const auto& r : v) {
        const auto insert_query = gen(r[0], r[1], r[2], r[3], r[4]);

        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS err_test;");
      run_ddl_statement(
          "CREATE TABLE err_test (x INT, y BIGINT, f FLOAT, d "
          "DOUBLE, x2 INT) WITH "
          "(FRAGMENT_SIZE=2);");

      TestHelpers::ValuesGenerator gen("err_test");

      for (int i = 0; i < 5; i++) {
        const auto insert_query = gen(std::numeric_limits<int32_t>::max() - 1,
                                      std::numeric_limits<int64_t>::max() - 1,
                                      std::numeric_limits<float>::max() - 1.0,
                                      std::numeric_limits<double>::max() - 1.0,
                                      i);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS time_test;");
      run_ddl_statement("CREATE TABLE time_test(t1 TIMESTAMP(9));");

      TestHelpers::ValuesGenerator gen("time_test");
      std::vector<std::string> v = {"'1971-01-01 01:01:01.001001001'",
                                    "'1972-02-02 02:02:02.002002002'",
                                    "'1973-03-03 03:03:03.003003003'"};

      for (const auto& s : v) {
        const auto insert_query = gen(s);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS time_test_castable;");
      run_ddl_statement("CREATE TABLE time_test_castable(t1 TIMESTAMP(6));");

      TestHelpers::ValuesGenerator gen("time_test_castable");
      std::vector<std::string> v = {"'1971-01-01 01:01:01.001001'",
                                    "'1972-02-02 02:02:02.002002'",
                                    "'1973-03-03 03:03:03.003003'"};

      for (const auto& s : v) {
        const auto insert_query = gen(s);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }

    {
      run_ddl_statement("DROP TABLE IF EXISTS time_test_uncastable;");
      run_ddl_statement("CREATE TABLE time_test_uncastable(t1 TIMESTAMP(6));");

      // nanosecond precision TIMESTAMPS can only represent years up to ~2262
      TestHelpers::ValuesGenerator gen("time_test_uncastable");
      std::vector<std::string> v = {"'2300-01-01 00:00:00.000000001'"};

      for (const auto& s : v) {
        const auto insert_query = gen(s);
        run_multiple_agg(insert_query, ExecutorDeviceType::CPU);
      }
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS arr_test;");
      run_ddl_statement(
          "CREATE TABLE arr_test ("
          "barr BOOLEAN[], sum_along_row_barr BOOLEAN, concat_barr BOOLEAN[], "
          "carr TINYINT[], sum_along_row_carr TINYINT, concat_carr TINYINT[], "
          "concat_carr_literal TINYINT[], "
          "sarr SMALLINT[], sum_along_row_sarr SMALLINT, concat_sarr SMALLINT[], "
          "concat_sarr_literal SMALLINT[], "
          "iarr INT[], sum_along_row_iarr INT, concat_iarr INT[], concat_iarr_literal "
          "INT[], "
          "larr BIGINT[], sum_along_row_larr BIGINT, concat_larr  BIGINT[], "
          "concat_larr_literal BIGINT[], "
          "farr FLOAT[], sum_along_row_farr FLOAT, concat_farr FLOAT[], "
          "concat_farr_literal FLOAT[], "
          "darr DOUBLE[], sum_along_row_darr DOUBLE, concat_darr DOUBLE[], "
          "concat_darr_literal DOUBLE[], "
          "tarr TEXT[] ENCODING DICT(32), sum_along_row_tarr TEXT ENCODING DICT(32), "
          "concat_tarr TEXT[] ENCODING DICT(32), "
          "l BIGINT, asarray_l BIGINT[],"
          "t TEXT ENCODING DICT(32), asarray_t TEXT[] ENCODING DICT(32)"
          ");");

      TestHelpers::ValuesGenerator gen("arr_test");

      run_multiple_agg(gen("{'true', 'true', 'false'}",
                           "'true'",
                           "{'true', 'true', 'false', 'true', 'true', 'false'}",
                           "{1, 2}",
                           3,
                           "{1, 2, 1, 2}",
                           "{1, 2, NULL, 99, 88}",
                           "{1, 2}",
                           3,
                           "{1, 2, 1, 2}",
                           "{1, 2, NULL, 99, 88}",
                           "{1, 2}",
                           3,
                           "{1, 2, 1, 2}",
                           "{1, 2, NULL, 99, 88}",
                           "{1, 2}",
                           3,
                           "{1, 2, 1, 2}",
                           "{1, 2, NULL, 99, 88}",
                           "{1.0, 2.0}",
                           "3.0",
                           "{1.0, 2.0, 1.0, 2.0}",
                           "{1.0, 2.0, NULL, 99.0, 88.0}",
                           "{1.0, 2.0}",
                           "3.0",
                           "{1.0, 2.0, 1.0, 2.0}",
                           "{1.0, 2.0, NULL, 99.0, 88.0}",
                           "{'a', 'bc'}",
                           "'abc'",
                           "{'a', 'bc', 'a', 'bc'}",
                           3,
                           "{3}",
                           "'ABC'",
                           "{'ABC'}"),
                       ExecutorDeviceType::CPU);
      run_multiple_agg(gen("{'true', NULL, 'false'}",
                           "'true'",
                           "{'true', NULL, 'false', 'true', NULL, 'false'}",
                           "{1, NULL, 2}",
                           3,
                           "{1, NULL, 2, 1, NULL, 2}",
                           "{1, NULL, 2, NULL, 99, 88}",
                           "{1, NULL, 2}",
                           3,
                           "{1, NULL, 2, 1, NULL, 2}",
                           "{1, NULL, 2, NULL, 99, 88}",
                           "{1, NULL, 2}",
                           3,
                           "{1, NULL, 2, 1, NULL, 2}",
                           "{1, NULL, 2, NULL, 99, 88}",
                           "{1, NULL, 2}",
                           3,
                           "{1, NULL, 2, 1, NULL, 2}",
                           "{1, NULL, 2, NULL, 99, 88}",
                           "{1.0, NULL, 2.0}",
                           "3.0",
                           "{1.0, NULL, 2.0, 1.0, NULL, 2.0}",
                           "{1.0, NULL, 2.0, NULL, 99.0, 88.0}",
                           "{1.0, NULL, 2.0}",
                           "3.0",
                           "{1.0, NULL, 2.0, 1.0, NULL, 2.0}",
                           "{1.0, NULL, 2.0, NULL, 99.0, 88.0}",
                           "{'a', NULL, 'bc'}",
                           "'abc'",
                           "{'a', NULL, 'bc', 'a', NULL, 'bc'}",
                           4,
                           "{4}",
                           "'DEF'",
                           "{'DEF'}"),
                       ExecutorDeviceType::CPU);
      run_multiple_agg(gen("{}",
                           "'false'",
                           "{}",
                           "{}",
                           "0",
                           "{}",
                           "{NULL, 99, 88}",
                           "{}",
                           "0",
                           "{}",
                           "{NULL, 99, 88}",
                           "{}",
                           "0",
                           "{}",
                           "{NULL, 99, 88}",
                           "{}",
                           "0",
                           "{}",
                           "{NULL, 99, 88}",
                           "{}",
                           "0.0",
                           "{}",
                           "{NULL, 99.0, 88.0}",
                           "{}",
                           "0.0",
                           "{}",
                           "{NULL, 99.0, 88.0}",
                           "{}",
                           "''",
                           "{}",
                           0,
                           "{0}",
                           "''",
                           "NULL"  // empty string is considered as NULL
                           ),
                       ExecutorDeviceType::CPU);
      run_multiple_agg(gen("NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "{NULL, 99, 88}",
                           "NULL",
                           "NULL",
                           "NULL",
                           "{NULL, 99, 88}",
                           "NULL",
                           "NULL",
                           "NULL",
                           "{NULL, 99, 88}",
                           "NULL",
                           "NULL",
                           "NULL",
                           "{NULL, 99, 88}",
                           "NULL",
                           "NULL",
                           "NULL",
                           "{NULL, 99.0, 88.0}",
                           "NULL",
                           "NULL",
                           "NULL",
                           "{NULL, 99.0, 88.0}",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL"),
                       ExecutorDeviceType::CPU);
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS geo_point_test;");
      run_ddl_statement(
          "CREATE TABLE geo_point_test("
          "p1 POINT, "
          "p2 GEOMETRY(POINT, 4326), "  // uses geoint compression
          "p3 GEOMETRY(POINT, 4326) ENCODING NONE, "
          "p4 GEOMETRY(POINT, 900913),"
          "l1 LINESTRING, "
          "l2 GEOMETRY(LINESTRING, 4326) ENCODING NONE, "
          "l3 GEOMETRY(LINESTRING, 4326) ENCODING NONE, "
          "l4 GEOMETRY(LINESTRING, 900913), "
          "mp1 MULTIPOINT, "
          "mp2 GEOMETRY(MULTIPOINT, 4326) ENCODING NONE, "
          "mp3 GEOMETRY(MULTIPOINT, 4326) ENCODING NONE, "
          "mp4 GEOMETRY(MULTIPOINT, 900913));");

      TestHelpers::ValuesGenerator gen("geo_point_test");

      run_multiple_agg(gen("'POINT(1 2)'",
                           "'POINT(3 4)'",
                           "'POINT(5 6)'",
                           "'POINT(7 8)'",
                           "'LINESTRING(1 2, 3 5)'",
                           "'LINESTRING(3 4, 5 7)'",
                           "'LINESTRING(5 6, 7 9)'",
                           "'LINESTRING(7 8, 9 11)'",
                           "'MULTIPOINT(1 2, 3 5)'",
                           "'MULTIPOINT(3 4, 5 7)'",
                           "'MULTIPOINT(5 6, 7 9)'",
                           "'MULTIPOINT(7 8, 9 11)'"),
                       ExecutorDeviceType::CPU);
      run_multiple_agg(gen("'POINT(9 8)'",
                           "'POINT(7 6)'",
                           "'POINT(5 4)'",
                           "'POINT(3 2)'",
                           "'LINESTRING(9 8, 11 11)'",
                           "'LINESTRING(7 6, 9 9)'",
                           "'LINESTRING(5 4, 7 7)'",
                           "'LINESTRING(3 2, 5 5)'",
                           "'MULTIPOINT(9 8, 11 11)'",
                           "'MULTIPOINT(7 6, 9 9)'",
                           "'MULTIPOINT(5 4, 7 7)'",
                           "'MULTIPOINT(3 2, 5 5)'"),
                       ExecutorDeviceType::CPU);
      run_multiple_agg(gen("NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL",
                           "NULL"),
                       ExecutorDeviceType::CPU);
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS geo_line_string_test;");
      run_ddl_statement(
          "CREATE TABLE geo_line_string_test("
          "l1 LINESTRING, "
          "l2 GEOMETRY(LINESTRING, 4326), "  // uses geoint compression
          "l3 GEOMETRY(LINESTRING, 4326) ENCODING NONE, "
          "l4 GEOMETRY(LINESTRING, 900913), "
          "mp1 MULTIPOINT, "
          "mp2 GEOMETRY(MULTIPOINT, 4326), "  // uses geoint compression
          "mp3 GEOMETRY(MULTIPOINT, 4326) ENCODING NONE, "
          "mp4 GEOMETRY(MULTIPOINT, 900913));");

      TestHelpers::ValuesGenerator gen("geo_line_string_test");

      run_multiple_agg(gen("'LINESTRING(1 2, 3 4)'",
                           "'LINESTRING(3 4, 5 6)'",
                           "'LINESTRING(5 6, 7 8)'",
                           "'LINESTRING(7 8, 9 0)'",
                           "'MULTIPOINT(1 2, 3 4)'",
                           "'MULTIPOINT(3 4, 5 6)'",
                           "'MULTIPOINT(5 6, 7 8)'",
                           "'MULTIPOINT(7 8, 9 0)'"),
                       ExecutorDeviceType::CPU);
      run_multiple_agg(gen("'LINESTRING(2 1, 4 3, 7 8)'",
                           "'LINESTRING(4 3, 6 5, 5 6)'",
                           "'LINESTRING(6 5, 8 7, 3 4)'",
                           "'LINESTRING(8 7, 0 9, 1 2)'",
                           "'MULTIPOINT(2 1, 4 3, 7 8)'",
                           "'MULTIPOINT(4 3, 6 5, 5 6)'",
                           "'MULTIPOINT(6 5, 8 7, 3 4)'",
                           "'MULTIPOINT(8 7, 0 9, 1 2)'"),
                       ExecutorDeviceType::CPU);
      run_multiple_agg(gen("'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'"),
                       ExecutorDeviceType::CPU);
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS geo_polygon_test;");
      run_ddl_statement(
          "CREATE TABLE geo_polygon_test("
          "p1 POLYGON, "
          "ml1 MULTILINESTRING, "
          "r1 LINESTRING, h1 LINESTRING, hh1 LINESTRING, "
          "p2 GEOMETRY(POLYGON, 4326), "           // uses geoint compression
          "ml2 GEOMETRY(MULTILINESTRING, 4326), "  // uses geoint compression
          "r2 GEOMETRY(LINESTRING, 4326), h2 GEOMETRY(LINESTRING, 4326), hh2 "
          "GEOMETRY(LINESTRING, 4326), "
          "p3 GEOMETRY(POLYGON, 4326) ENCODING NONE, "
          "ml3 GEOMETRY(MULTILINESTRING, 4326) ENCODING NONE, "
          "r3 GEOMETRY(LINESTRING, 4326) ENCODING NONE, h3 GEOMETRY(LINESTRING, 4326) "
          "ENCODING NONE, "
          "hh3 GEOMETRY(LINESTRING, 4326) ENCODING NONE, "
          "p4 GEOMETRY(POLYGON, 900913),"
          "ml4 GEOMETRY(MULTILINESTRING, 900913),"
          "r4 GEOMETRY(LINESTRING, 900913), h4 GEOMETRY(LINESTRING, 900913), hh4 "
          "GEOMETRY(LINESTRING, 900913), "
          "mp1 MULTIPOLYGON, "
          "mp2 GEOMETRY(MULTIPOLYGON, 4326), "
          "mp3 GEOMETRY(MULTIPOLYGON, 4326) ENCODING NONE, "
          "mp4 GEOMETRY(MULTIPOLYGON, 900913), "
          "sizes INT);");

      TestHelpers::ValuesGenerator gen("geo_polygon_test");

      run_multiple_agg(
          gen("'POLYGON((1 2,3 4,5 6,7 8,9 10),(2 3,3 4,1 2))'",
              "'MULTILINESTRING((1 2,3 4,5 6,7 8,9 10),(2 3,3 4,1 2))'",
              "'LINESTRING(1 2,3 4,5 6,7 8,9 10)'",
              "'LINESTRING(2 3,3 4,1 2)'",
              "'NULL'",
              "'POLYGON((0 0,5 0,5 5,0 5,0 0),(2 2, 2 1,1 1,1 2,2 2))'",
              "'MULTILINESTRING((0 0,5 0,5 5,0 5),(2 2, 2 1,1 1,1 2))'",
              "'LINESTRING(0 0,5 0,5 5,0 5)'",
              "'LINESTRING(2 2,2 1,1 1,1 2)'",
              "'NULL'",
              "'POLYGON((0 0,6 0,6 6,0 6,0 0),(3 3,3 2,2 2,2 3,3 3))'",
              "'MULTILINESTRING((0 0,6 0,6 6,0 6),(3 3,3 2,2 2,2 3))'",
              "'LINESTRING(0 0,6 0,6 6,0 6))'",
              "'LINESTRING(3 3,3 2,2 2,2 3)'",
              "'NULL'",
              "'POLYGON((0 0,7 0,7 7,0 7,0 0),(4 4,2 4, 2 3,4 2,4 4))'",
              "'MULTILINESTRING((0 0,7 0,7 7,0 7),(4 4,2 4, 2 3,4 2))'",
              "'LINESTRING(0 0,7 0,7 7,0 7)'",
              "'LINESTRING(4 4,4 2,2 3,2 4)'",
              "'NULL'",
              "'MULTIPOLYGON(((1 2,3 4,5 6,7 8,9 10),(2 3,3 4,1 2)))'",
              "'MULTIPOLYGON(((0 0,5 0,5 5,0 5,0 0),(2 2, 2 1,1 1,1 2,2 2)))'",
              "'MULTIPOLYGON(((0 0,6 0,6 6,0 6,0 0),(3 3,3 2,2 2,2 3,3 3)))'",
              "'MULTIPOLYGON(((0 0,7 0,7 7,0 7,0 0),(4 4,2 4, 2 3,4 2,4 4)))'",
              "8"),
          ExecutorDeviceType::CPU);

      run_multiple_agg(gen("'POLYGON((0 0,5 0,5 5,0 5,0 0))'",
                           "'MULTILINESTRING((0 0,5 0,5 5,0 5))'",
                           "'LINESTRING(0 0,5 0,5 5,0 5)'",
                           "'NULL'",
                           "'NULL'",
                           "'POLYGON((0 0,6 0,6 6,0 6,0 0))'",
                           "'MULTILINESTRING((0 0,6 0,6 6,0 6))'",
                           "'LINESTRING(0 0,6 0,6 6,0 6)'",
                           "'NULL'",
                           "'NULL'",
                           "'POLYGON((0 0,7 0,7 7,0 7,0 0))'",
                           "'MULTILINESTRING((0 0,7 0,7 7,0 7))'",
                           "'LINESTRING(0 0,7 0,7 7,0 7)'",
                           "'NULL'",
                           "'NULL'",
                           "'POLYGON((0 0,4 0,4 4,0 4,0 0))'",
                           "'MULTILINESTRING((0 0,4 0,4 4,0 4))'",
                           "'LINESTRING(0 0,4 0,4 4,0 4)'",
                           "'NULL'",
                           "'NULL'",
                           "'MULTIPOLYGON(((0 0,5 0,5 5,0 5,0 0)))'",
                           "'MULTIPOLYGON(((0 0,6 0,6 6,0 6,0 0)))'",
                           "'MULTIPOLYGON(((0 0,7 0,7 7,0 7,0 0)))'",
                           "'MULTIPOLYGON(((0 0,4 0,4 4,0 4,0 0)))'",
                           "4"),
                       ExecutorDeviceType::CPU);

      run_multiple_agg(
          gen("'POLYGON((1 2,3 4,5 6,7 8,9 10),(3 4,1 2,2 3),(5 6,7 8,9 10))'",
              "'MULTILINESTRING((1 2,3 4,5 6,7 8,9 10),(3 4,1 2,2 3),(5 6,7 8,9 10))'",
              "'LINESTRING(1 2,3 4,5 6,7 8,9 10)'",
              "'LINESTRING(2 3,3 4,1 2)'",
              "'LINESTRING(9 10,5 6,7 8)'",
              "'POLYGON((0 0,5 0,5 5,0 5,0 0),(2 2,2 1,1 1,1 2,2 2),(0 0,0 1,1 0))'",
              "'MULTILINESTRING((0 0,5 0,5 5,0 5),(2 2,2 1,1 1,1 2),(0 0,0 1,1 0))'",
              "'LINESTRING(0 0,5 0,5 5,0 5)'",
              "'LINESTRING(2 2,2 1,1 1,1 2)'",
              "'LINESTRING(0 0,0 1,1 0)'",
              "'POLYGON((0 0,6 0,6 6,0 6,0 0),(3 3,3 2,2 2,2 3,3 3),(0 0,0 1,1 0))'",
              "'MULTILINESTRING((0 0,6 0,6 6,0 6),(3 3,3 2,2 2,2 3),(0 0,0 1,1 0))'",
              "'LINESTRING(0 0,6 0,6 6,0 6))'",
              "'LINESTRING(3 3,3 2,2 2,2 3)'",
              "'LINESTRING(0 0,0 1,1 0)'",
              "'POLYGON((0 0,7 0,7 7,0 7,0 0),(4 4,2 4, 2 3,4 2,4 4),(0 0,0 1,1 0))'",
              "'MULTILINESTRING((0 0,7 0,7 7,0 7),(4 4,2 4, 2 3,4 2),(0 0,0 1,1 0))'",
              "'LINESTRING(0 0,7 0,7 7,0 7)'",
              "'LINESTRING(4 4,4 2,2 3,2 4)'",
              "'LINESTRING(0 0,0 1,1 0)'",
              "'MULTIPOLYGON(((1 2,3 4,5 6,7 8,9 10),(3 4,1 2,2 3),(5 6,7 8,9 10)))'",
              "'MULTIPOLYGON(((0 0,5 0,5 5,0 5,0 0),(2 2,2 1,1 1,1 2,2 2),(0 0,0 1,1 "
              "0)))'",
              "'MULTIPOLYGON(((0 0,6 0,6 6,0 6,0 0),(3 3,3 2,2 2,2 3,3 3),(0 0,0 1,1 "
              "0)))'",
              "'MULTIPOLYGON(((0 0,7 0,7 7,0 7,0 0),(4 4,2 4, 2 3,4 2,4 4),(0 0,0 1,1 "
              "0)))'",
              "11"),
          ExecutorDeviceType::CPU);
      run_multiple_agg(gen("'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "'NULL'",
                           "NULL"),
                       ExecutorDeviceType::CPU);
    }
    {
      run_ddl_statement("DROP TABLE IF EXISTS bytes_test;");
      run_ddl_statement(
          "CREATE TABLE bytes_test ("
          "t1 TEXT ENCODING NONE, t2 TEXT ENCODING NONE, t12 TEXT ENCODING NONE, t1abc "
          "TEXT ENCODING NONE, abct1 TEXT ENCODING NONE);");
      TestHelpers::ValuesGenerator gen("bytes_test");

      run_multiple_agg(gen("'A', 'B', 'AB', 'Aabc', 'abcA'"), ExecutorDeviceType::CPU);
      run_multiple_agg(gen("'B', 'CD', 'BCD', 'Babc', 'abcB'"), ExecutorDeviceType::CPU);
      run_multiple_agg(gen("'C', NULL, 'C', 'Cabc', 'abcC'"), ExecutorDeviceType::CPU);
      run_multiple_agg(gen("NULL, 'D', 'D', NULL, NULL"), ExecutorDeviceType::CPU);
      run_multiple_agg(gen("NULL, NULL, NULL, NULL, NULL"), ExecutorDeviceType::CPU);
    }
  }

  void TearDown() override {
    run_ddl_statement("DROP TABLE IF EXISTS tf_test;");
    run_ddl_statement("DROP TABLE IF EXISTS sd_test;");
    run_ddl_statement("DROP TABLE IF EXISTS err_test");
    run_ddl_statement("DROP TABLE IF EXISTS time_test");
    run_ddl_statement("DROP TABLE IF EXISTS time_test_castable");
    run_ddl_statement("DROP TABLE IF EXISTS time_test_uncastable");
    run_ddl_statement("DROP TABLE IF EXISTS arr_test;");
    run_ddl_statement("DROP TABLE IF EXISTS geo_point_test;");
    run_ddl_statement("DROP TABLE IF EXISTS geo_line_string_test;");
    run_ddl_statement("DROP TABLE IF EXISTS geo_polygon_test;");
    run_ddl_statement("DROP TABLE IF EXISTS bytes_test;");
  }
};

TEST_F(TableFunctions, BasicProjection) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test), 0)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(0));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test), 1)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test), 2)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test), 3)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(15));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test), 4)) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(20));
    }
    if (dt == ExecutorDeviceType::CPU) {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d "
          "FROM tf_test), 0)) ORDER "
          "BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(0));
    }
    if (dt == ExecutorDeviceType::CPU) {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d "
          "FROM tf_test), 1)) ORDER "
          "BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_adder(1, "
          "cursor(SELECT d, d2 FROM tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_adder(4, "
          "cursor(SELECT d, d2 FROM tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(20));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, out1 FROM TABLE(row_addsub(1, cursor(SELECT d, d2 FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Omit sizer (kRowMultiplier)
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_adder(cursor(SELECT d, "
          "d2 FROM tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test))) ORDER BY "
          "out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Constant (kConstant) size tests with get_max_with_row_offset
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(get_max_with_row_offset(cursor(SELECT x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]),
                static_cast<int64_t>(4));  // max value of x
    }
    {
      // swap output column order
      const auto rows = run_multiple_agg(
          "SELECT out1, out0 FROM "
          "TABLE(get_max_with_row_offset(cursor(SELECT x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]),
                static_cast<int64_t>(4));  // row offset of max x
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]),
                static_cast<int64_t>(4));  // max value of x
    }
    // Table Function specified sizer test
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(column_list_row_sum(cursor(SELECT x, x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
    }
    // TextEncodingDict specific tests
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT base FROM "
          "sd_test),"
          "1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"hello", "foo", "bar", "world", "baz"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg("SELECT base FROM sd_test;", dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"hello", "foo", "bar", "world", "baz"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT derived FROM "
          "sd_test),"
          "1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"world", "bar", "baz", "foo", "hello"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }

    // Test boolean scalars AND return of less rows than allocated in table
    // function
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(sort_column_limit(CURSOR(SELECT x FROM "
          "tf_test), 2, "
          "true, "
          "true)) ORDER by out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      std::vector<int64_t> expected_result_set{0, 1};
      for (size_t i = 0; i < 2; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(sort_column_limit(CURSOR(SELECT x FROM "
          "tf_test), 3, "
          "false, "
          "true)) ORDER by out0 DESC;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(3));
      std::vector<int64_t> expected_result_set{4, 3, 2};
      for (size_t i = 0; i < 3; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require(cursor(SELECT x"
          " FROM tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 3);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_str(cursor(SELECT x"
          " FROM tf_test), 'hello'));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 3);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_templating(cursor(SELECT x"
          " FROM tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 5);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_templating(cursor(SELECT d"
          " FROM tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 6.0);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_and(cursor(SELECT x"
          " FROM tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 7);
    }
    {
      std::vector<std::string> strs = {"MIN", "MAX"};
      for (const std::string& str : strs) {
        const auto rows = run_multiple_agg(
            "SELECT * FROM TABLE(ct_require_or_str(cursor(SELECT x"
            " FROM tf_test), '" +
                str + "'));",
            dt);
        ASSERT_EQ(rows->rowCount(), size_t(1));
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, 8);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_str_diff(cursor(SELECT x"
          " FROM tf_test), 'MIN'));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 9);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_text_enc_dict(cursor(SELECT t2"
          " FROM sd_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 10);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_text_collist_enc_dict(cursor(SELECT t2, t2, t2"
          " FROM sd_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 11);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_cursor(cursor(SELECT x"
          " FROM tf_test), 6));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 12);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_test_allocator(cursor(SELECT x"
          " FROM tf_test), 'hello'));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 11);
    }
    {
      if (dt == ExecutorDeviceType::GPU) {
        const auto rows = run_multiple_agg(
            "SELECT * FROM TABLE(ct_require_device_cuda(cursor(SELECT x"
            " FROM tf_test), 2));",
            dt);
        ASSERT_EQ(rows->rowCount(), size_t(1));
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, 12345);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_require_range(cursor(SELECT x"
          " FROM tf_test), 3, 1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, 0);
    }

    // Test for columns containing null values (QE-163)
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_test_nullable(cursor(SELECT x from "
          "tf_test), 1)) "
          "where out0 is not null;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      std::vector<int64_t> expected_result_set{1, 3};
      for (size_t i = 0; i < 2; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    // Test for pre-flight sizer (QE-179)
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_test_preflight_sizer(cursor(SELECT x from "
          "tf_test), "
          "0, 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      std::vector<int64_t> expected_result_set{123, 456};
      for (size_t i = 0; i < 2; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_test_preflight_sizer_const(cursor(SELECT "
          "x from "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      std::vector<int64_t> expected_result_set{789, 321};
      for (size_t i = 0; i < 2; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    // Test for bug (QE-227)
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_test_preflight_singlecursor_qe227(cursor(SELECT x, "
          "x+10, "
          "x+20 from "
          "tf_test), "
          "200, 50));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(3));
      std::vector<int64_t> expected_result_set{0, 10, 20};
      for (size_t i = 0; i < 3; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_test_preflight_multicursor_qe227(cursor(SELECT x from "
          "tf_test), "
          "cursor(SELECT x+30, x+40 from tf_test), "
          "200, 50));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(3));
      std::vector<int64_t> expected_result_set{1, 31, 41};
      for (size_t i = 0; i < 3; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    // Tests various invalid returns from a table function:
    if (dt == ExecutorDeviceType::CPU) {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d "
          "FROM tf_test), -1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(0));
    }

    if (dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d "
                                    "FROM tf_test), -2));",
                                    dt),
                   std::runtime_error);
    }
  }
}

TEST_F(TableFunctions, TestCalciteOverload) {
  // Test for QE-646 as calcite is failing with a NullPointerException
  for (auto dt : {ExecutorDeviceType::CPU}) {
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_test_func(cursor(SELECT x from tf_test), 1, 1));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      std::vector<int64_t> expected_result_set{123};
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, expected_result_set[0]);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_test_func(cursor(SELECT x from tf_test), cursor(SELECT "
          "x from tf_test), 1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      std::vector<int64_t> expected_result_set{234};
      auto row = rows->getNextRow(true, false);
      auto v = TestHelpers::v<int64_t>(row[0]);
      ASSERT_EQ(v, expected_result_set[0]);
    }
  }
}

TEST_F(TableFunctions, GpuDefaultOutputInitializaiton) {
  for (auto dt : {ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const std::string query = "SELECT * FROM TABLE(ct_gpu_default_init());";
      const auto rows = run_multiple_agg(query, dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      ASSERT_EQ(rows->colCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(0));
    }
  }
}

TEST_F(TableFunctions, GpuThreads) {
  for (auto dt : {ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto cuda_block_size = QR::get()->getExecutor()->blockSize();
      const auto cuda_grid_size = QR::get()->getExecutor()->gridSize();
      const size_t total_threads = cuda_block_size * cuda_grid_size;
      const std::string query = "SELECT * FROM TABLE(ct_cuda_enumerate_threads(" +
                                std::to_string(total_threads) +
                                ")) ORDER by global_thread_id ASC;";
      const auto rows = run_multiple_agg(query, dt);
      ASSERT_EQ(rows->rowCount(), size_t(total_threads));
      ASSERT_EQ(rows->colCount(), size_t(3));
      for (size_t t = 0; t < total_threads; ++t) {
        auto crt_row = rows->getNextRow(true, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]),
                  static_cast<int64_t>(t % cuda_block_size));
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]),
                  static_cast<int64_t>(t / cuda_block_size));
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[2]), static_cast<int64_t>(t));
      }
    }
  }
}

TEST_F(TableFunctions, GroupByIn) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test GROUP BY d), "
          "1)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test GROUP BY d), "
          "2)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test GROUP BY d), "
          "3)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(15));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier(cursor(SELECT d "
          "FROM tf_test GROUP BY d), "
          "4)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(20));
    }
  }
}

TEST_F(TableFunctions, NamedArgsMissingArgs) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      // Should throw when using named argument syntax if argument is missing
      // Scalar args test
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_scalar_named_args"
                                    "(arg1 => 1));",
                                    dt),
                   std::exception);

      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_scalar_named_args"
                                    "(arg2 => 2));",
                                    dt),
                   std::exception);
    }

    {
      // Should throw when using named argument syntax if argument is missing
      // Cursor test
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_cursor_named_args"
                                    "(arg1 => 3, arg2 => 1));",
                                    dt),
                   std::exception);

      EXPECT_THROW(run_multiple_agg(
                       "SELECT * FROM TABLE(ct_cursor_named_args"
                       "(CURSOR(SELECT * from (VALUES (1, 2)) AS t(x, y), arg2 => 1));",
                       dt),
                   std::exception);

      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_cursor_named_args"
                                    "(input_table => CURSOR(SELECT * from (VALUES (1, "
                                    "2)) AS t(x, y), arg1 => 1));",
                                    dt),
                   std::exception);
    }

    {
      // Should throw when using named argument syntax if arguments
      // are present but one is not named
      // Scalar args test
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_scalar_named_args"
                                    "(arg1 => 1, 2));",
                                    dt),
                   std::exception);

      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_scalar_named_args"
                                    "(1, arg2 = >2));",
                                    dt),
                   std::exception);
    }

    {
      // Should throw when using named argument syntax if arguments
      // are present but one is not named
      // Cursor test
      EXPECT_THROW(run_multiple_agg(
                       "SELECT * FROM TABLE(ct_cursor_named_args("
                       "CURSOR(SELECT * from (VALUES (1, 2))), arg1 => 1, arg2 => 2));",
                       dt),
                   std::exception);

      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_cursor_named_args("
                           "input_table => CURSOR(SELECT * from (VALUES (1, 2))), "
                           "arg1 => 1,  2));",
                           dt),
          std::exception);
    }
  }
}

TEST_F(TableFunctions, NamedArgsValidCalls) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Scalar args
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_scalar_named_args"
          "(arg1 => 1, arg2 => 2));",
          dt);
      EXPECT_EQ(rows->rowCount(), size_t(1));
      EXPECT_EQ(rows->colCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[0]), 1);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[1]), 2);
    }

    // Scalar args - reverse order
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_scalar_named_args"
          "(arg2 => 4, arg1 => 3));",
          dt);
      EXPECT_EQ(rows->rowCount(), size_t(1));
      EXPECT_EQ(rows->colCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[0]), 3);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[1]), 4);
    }

    // Cursor arg
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_cursor_named_args("
          "input_table => CURSOR(SELECT * from (VALUES (1, 2))), "
          "arg1 => 1,  arg2=>2));",
          dt);
      EXPECT_EQ(rows->rowCount(), size_t(1));
      EXPECT_EQ(rows->colCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[0]), 2);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[1]), 4);
    }
    // Cursor arg - permutation 2
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_cursor_named_args("
          "arg2 => 3, input_table => CURSOR(SELECT * from (VALUES (1, 2))), "
          "arg1=>10));",
          dt);
      EXPECT_EQ(rows->rowCount(), size_t(1));
      EXPECT_EQ(rows->colCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[0]), 11);
      EXPECT_EQ(TestHelpers::v<int64_t>(crt_row[1]), 5);
    }
  }
}

TEST_F(TableFunctions, GroupByOut) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      // Tests QE-240 output column width mismatch fix
      const auto rows = run_multiple_agg(
          "SELECT out0, COUNT(*) AS n FROM(SELECT * FROM "
          "TABLE(row_copier_text(CURSOR(SELECT base FROM sd_test ORDER BY "
          "KEY_FOR_STRING(base) LIMIT 2), 2))) GROUP BY out0 ORDER by out0;",
          dt);
      std::vector<std::string> expected_out0{"hello", "world"};
      std::vector<int64_t> expected_n{2, 2};
      ASSERT_EQ(rows->rowCount(), size_t(2));
      for (size_t i = 0; i < 2; i++) {
        auto row = rows->getNextRow(true, false);
        auto out0 = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        auto n = TestHelpers::v<int64_t>(row[1]);
        ASSERT_EQ(out0, expected_out0[i]);
        ASSERT_EQ(n, expected_n[i]);
      }
    }
  }
}

TEST_F(TableFunctions, GroupByInAndOut) {
  auto check_result = [](const auto rows, const size_t copies) {
    ASSERT_EQ(rows->rowCount(), size_t(5));
    for (size_t i = 0; i < 5; i++) {
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(copies));
    }
  };

  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM "
          "TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "1)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 1);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM "
          "TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "2)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 2);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM "
          "TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "3)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 3);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0, count(*) FROM "
          "TABLE(row_copier(cursor(SELECT d FROM tf_test), "
          "4)) "
          "GROUP BY out0 ORDER BY out0;",
          dt);
      check_result(rows, 4);
    }
    // TextEncodingDict specific tests
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT base FROM "
          "sd_test),"
          "1)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"bar", "baz", "foo", "hello", "world"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(row_copier_text(cursor(SELECT derived FROM "
          "sd_test),"
          "1)) "
          "ORDER BY out0;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"bar", "baz", "foo", "hello", "world"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
  }
}

TEST_F(TableFunctions, ConstantCasts) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Numeric constant to float
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT f "
          "FROM "
          "tf_test), 2.2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Numeric constant to double
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT d "
          "FROM "
          "tf_test), 2.2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Integer constant to double
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT d "
          "FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Numeric (integer) constant to double
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT d "
          "FROM "
          "tf_test), 2.));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Integer constant
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT x "
          "FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    // Should throw: Numeric constant to integer
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT out0 FROM "
                           "TABLE(ct_binding_scalar_multiply(CURSOR(SELECT x FROM "
                           "tf_test), 2.2));",
                           dt),
          std::exception);
    }
    // Should throw: boolean constant to integer
    {
      EXPECT_THROW(run_multiple_agg(
                       "SELECT out0 FROM TABLE(ct_binding_scalar_multiply(CURSOR(SELECT "
                       "x FROM tf_test), true));",
                       dt),
                   std::invalid_argument);
    }
  }
}

TEST_F(TableFunctions, Template) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT x FROM "
          "tf_test), "
          "cursor(SELECT d from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT d FROM "
          "tf_test), "
          "cursor(SELECT d2 from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(20));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT x FROM "
          "tf_test), "
          "cursor(SELECT x from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(30));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT d FROM "
          "tf_test), "
          "cursor(SELECT x from tf_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(40));
    }
    // TextEncodingDict
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_binding_column2(cursor(SELECT base FROM "
          "sd_test),"
          "cursor(SELECT derived from sd_test)))",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
      std::vector<std::string> expected_result_set{"hello", "foo", "bar", "world", "baz"};
      for (size_t i = 0; i < 5; i++) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(s, expected_result_set[i]);
      }
    }
  }
}

TEST_F(TableFunctions, Unsupported) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();

    EXPECT_ANY_THROW(
        run_multiple_agg("select * from table(row_copier(cursor(SELECT d, "
                         "cast(x as double) FROM tf_test), 2));",
                         dt));
  }
}

TEST_F(TableFunctions, CallFailure) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();

    EXPECT_THROW(run_multiple_agg("SELECT out0 FROM TABLE(row_copier(cursor("
                                  "SELECT d FROM tf_test),101));",
                                  dt),
                 UserTableFunctionError);

    // Skip this test for GPU. TODO: row_copier return value is ignored.
    break;
  }
}

TEST_F(TableFunctions, CountDistinctOverRowCopiedTable) {
  const auto testQuery = [](const std::string& query, ExecutorDeviceType dt) {
    const auto res = run_multiple_agg(query, dt);
    ASSERT_EQ(res->rowCount(), size_t(1));
    auto crt_row = res->getNextRow(false, false);
    ASSERT_GT(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(0));
  };
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();

    testQuery(
        "SELECT approx_count_distinct(out0) FROM TABLE(row_copier(cursor(SELECT d FROM "
        "tf_test), 1));",
        dt);
    testQuery(
        "SELECT count(distinct out0) FROM TABLE(row_copier(cursor(SELECT d FROM "
        "tf_test), 1));",
        dt);
    break;
  }
}

TEST_F(TableFunctions, NamedOutput) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_output(cursor(SELECT d FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<double>(crt_row[0]), static_cast<double>(11));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_const_output(cursor(SELECT x FROM "
          "tf_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(6));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(4));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_user_const_output(cursor(SELECT x "
          "FROM "
          "tf_test), 1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(10));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_user_const_output(cursor(SELECT x "
          "FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(6));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(4));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_rowmul_output(cursor(SELECT x FROM "
          "tf_test), 1));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT total FROM TABLE(ct_named_rowmul_output(cursor(SELECT x FROM "
          "tf_test), 2));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));
    }
  }
}

TEST_F(TableFunctions, CursorlessInputs) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT answer FROM TABLE(ct_no_arg_constant_sizing()) ORDER BY "
          "answer;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(42));
      for (size_t i = 0; i < 42; i++) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(42 * i));
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT answer / 882 AS g, COUNT(*) AS n FROM "
          "TABLE(ct_no_arg_constant_sizing()) GROUP BY g ORDER BY g;",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(2));

      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(0));
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(21));

      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(1));
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(21));
    }

    {
      const auto rows =
          run_multiple_agg("SELECT answer FROM TABLE(ct_no_arg_runtime_sizing());", dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(42));
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT answer FROM TABLE(ct_scalar_1_arg_runtime_sizing(123));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(3));

      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(123));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(12));
      crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(1));
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT answer1, answer2 FROM "
          "TABLE(ct_scalar_2_args_constant_sizing(100, "
          "5));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));

      for (size_t r = 0; r < 5; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(100 + r * 5));
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), static_cast<int64_t>(100 - r * 5));
      }
    }

    // Tests for user-defined constant parameter sizing, which were separately
    // broken from the above
    {
      const auto rows = run_multiple_agg(
          "SELECT output FROM TABLE(ct_no_cursor_user_constant_sizer(8, 10));", dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));

      for (size_t r = 0; r < 10; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(8));
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT output FROM "
          "TABLE(ct_templated_no_cursor_user_constant_sizer(7, 4));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(4));

      for (size_t r = 0; r < 4; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(7));
      }
    }
  }
}

TEST_F(TableFunctions, DictionaryReadAccess) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Column access to string dictionary proxy

    auto len_test = [](const std::shared_ptr<ResultSet>& rows,
                       const std::vector<std::string>& expected_result_set) {
      ASSERT_EQ(rows->colCount(), size_t(2));  // string and length
      const size_t num_rows{rows->rowCount()};
      ASSERT_EQ(num_rows, expected_result_set.size());
      for (size_t r = 0; r < num_rows; ++r) {
        auto row = rows->getNextRow(true, false);
        auto s = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        const int64_t len = TestHelpers::v<int64_t>(row[1]);
        ASSERT_EQ(s, expected_result_set[r]);
        ASSERT_GE(len, 0L);
        ASSERT_EQ(static_cast<size_t>(len), s.size());
      }
    };

    {
      // Test default TEXT ENCODING DICT(32) access
      const auto rows = run_multiple_agg(
          "SELECT string, string_length FROM TABLE(ct_binding_str_length(cursor(SELECT "
          "t1 FROM "
          "sd_test))) ORDER BY string;",
          dt);
      const std::vector<std::string> expected_result_set{
          "California", "New York", "New York", "New York", "Ohio"};
      len_test(rows, expected_result_set);
    }

    {
      // Test shared dict access
      const auto rows = run_multiple_agg(
          "SELECT string, string_length FROM TABLE(ct_binding_str_length(cursor(SELECT "
          "t2 FROM "
          "sd_test))) ORDER BY string;",
          dt);
      const std::vector<std::string> expected_result_set{
          "California", "Indiana", "New York", "Ohio", "Ohio"};
      len_test(rows, expected_result_set);
    }

    {
      // Test TEXT ENCODING DICT(8) access
      const auto rows = run_multiple_agg(
          "SELECT string, string_length FROM TABLE(ct_binding_str_length(cursor(SELECT "
          "t3 FROM "
          "sd_test))) ORDER BY string;",
          dt);
      const std::vector<std::string> expected_result_set{
          "California", "California", "Indiana", "New York", "North Carolina"};
      len_test(rows, expected_result_set);
    }

    {
      // Test Transient Dictionary Input
      const auto rows = run_multiple_agg(
          "SELECT string, string_length FROM TABLE(ct_binding_str_length(cursor(SELECT "
          "t1 || ' | ' || t2 FROM "
          "sd_test))) ORDER BY string;",
          dt);
      const std::vector<std::string> expected_result_set{"California | California",
                                                         "New York | Indiana",
                                                         "New York | New York",
                                                         "New York | Ohio",
                                                         "Ohio | Ohio"};
      len_test(rows, expected_result_set);
    }

    {
      // Test ability to equality check between strings
      const auto rows = run_multiple_agg(
          "SELECT string_if_equal, strings_are_equal FROM "
          "TABLE(ct_binding_str_equals(cursor(SELECT t1, t2, t3 FROM "
          "sd_test))) WHERE string_if_equal IS NOT NULL ORDER BY string_if_equal NULLS "
          "LAST;",
          dt);
      const std::vector<std::string> expected_result_strings{"California", "New York"};
      ASSERT_EQ(rows->rowCount(), size_t(2));
      for (size_t r = 0; r < 2; ++r) {
        auto row = rows->getNextRow(true, false);
        auto str = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        auto is_equal = boost::get<int64_t>(TestHelpers::v<int64_t>(row[1]));
        ASSERT_EQ(str, expected_result_strings[r]);
        ASSERT_EQ(is_equal, 1);
      }
    }
  }
}

TEST_F(TableFunctions, DictionaryWriteAccess) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();

    {
      // Test write to one column sharing output dictionary with input column
      const auto rows = run_multiple_agg(
          "SELECT substr, COUNT(*) AS n, ANY_VALUE(KEY_FOR_STRING(substr)) AS str_key "
          "FROM TABLE(ct_substr(CURSOR(SELECT t1, 0, 4 FROM sd_test))) GROUP BY substr "
          "ORDER by substr;",
          dt);
      const std::vector<std::string> expected_result_strings{"Cali", "New ", "Ohio"};
      const std::vector<int64_t> expected_result_counts{1, 3, 1};
      ASSERT_EQ(rows->rowCount(), size_t(3));
      ASSERT_EQ(rows->colCount(), size_t(3));
      for (size_t r = 0; r < 3; ++r) {
        auto row = rows->getNextRow(true, false);
        auto str = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        auto count = boost::get<int64_t>(TestHelpers::v<int64_t>(row[1]));
        auto str_key = boost::get<int64_t>(TestHelpers::v<int64_t>(row[2]));
        ASSERT_EQ(str, expected_result_strings[r]);
        ASSERT_EQ(count, expected_result_counts[r]);
        if (r < 2) {
          ASSERT_LE(str_key, -2);  // "Cali" and "New " should have temp dictionary ids
                                   // since they are not in the original dictionary
        } else {
          ASSERT_GE(str_key, 0);  // "Ohio" have regular dictioanry id
        }
      }
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT concatted_str FROM TABLE(ct_string_concat(CURSOR(SELECT t1, t2, t3 "
          "FROM sd_test), '|')) ORDER BY concatted_str;",
          dt);
      const auto rows_with_default_arg = run_multiple_agg(
          "SELECT concatted_str FROM TABLE(ct_string_concat(inp0 => CURSOR(SELECT t1, "
          "t2, t3 "
          "FROM sd_test))) ORDER BY concatted_str;",
          dt);
      const std::vector<std::string> expected_result_strings{
          "California|California|California",
          "New York|Indiana|Indiana",
          "New York|New York|New York",
          "New York|Ohio|California",
          "Ohio|Ohio|North Carolina"};
      ASSERT_EQ(rows->rowCount(), expected_result_strings.size());
      ASSERT_EQ(rows->colCount(), size_t(1));
      ASSERT_EQ(rows_with_default_arg->rowCount(), expected_result_strings.size());
      ASSERT_EQ(rows_with_default_arg->colCount(), size_t(1));
      for (size_t r = 0; r < expected_result_strings.size(); ++r) {
        auto row = rows->getNextRow(true, false);
        auto default_row = rows_with_default_arg->getNextRow(true, false);
        auto str = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        auto default_str =
            boost::get<std::string>(TestHelpers::v<NullableString>(default_row[0]));
        ASSERT_EQ(str, expected_result_strings[r]);
        ASSERT_EQ(default_str, expected_result_strings[r]);
      }
    }

    {
      // Test creating a new dictionary (i.e. dictionary is created for output column for
      // which there is no input)
      const auto rows = run_multiple_agg(
          "SELECT new_dict_col FROM TABLE(ct_synthesize_new_dict(3)) ORDER BY "
          "new_dict_col;",
          dt);
      ASSERT_EQ(rows->rowCount(), 3UL);
      ASSERT_EQ(rows->colCount(), size_t(1));
      for (size_t r = 0; r < 3; ++r) {
        auto row = rows->getNextRow(true, false);
        auto str = boost::get<std::string>(TestHelpers::v<NullableString>(row[0]));
        ASSERT_EQ(str, "String_" + std::to_string(r));
      }
    }
  }
}

TEST_F(TableFunctions, TextEncodingNoneLiteralArgs) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Following tests ability to transform to std::string running on CPU (runs
    // on CPU only)
    {
      const std::string test_string{"this is only a test"};
      const size_t test_string_len{test_string.size()};
      const std::string test_query(
          "SELECT char_idx, char_bytes FROM TABLE(ct_string_to_chars('" + test_string +
          "')) ORDER BY char_idx;");
      const auto rows = run_multiple_agg(test_query, dt);
      ASSERT_EQ(rows->rowCount(), test_string_len);
      for (size_t r = 0; r < test_string_len; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(r));
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]),
                  static_cast<int64_t>(test_string[r]));
      }
    }
    // Following tests two text encoding none input, plus running on GPU + CPU
    if (dt == ExecutorDeviceType::CPU) {
      const std::string test_string1{"theater"};
      const std::string test_string2{"theatre"};
      const std::string test_query(
          "SELECT hamming_distance FROM TABLE(ct_hamming_distance('" + test_string1 +
          "','" + test_string2 + "'));");
      const auto rows = run_multiple_agg(test_query, dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(2));
    }

    // Following tests varchar element accessors and that TextEncodedNone
    // literal inputs play nicely with column inputs + RowMultiplier
    {
      const std::string test_string{"theater"};
      const std::string test_query(
          "SELECT idx, char_bytes FROM TABLE(ct_get_string_chars(CURSOR(SELECT "
          "x FROM "
          "tf_test), '" +
          test_string + "', 1)) ORDER BY idx;");
      const auto rows = run_multiple_agg(test_query, dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));  // size of tf_test
      for (size_t r = 0; r < 5; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(r));
        ASSERT_EQ(
            TestHelpers::v<int64_t>(crt_row[1]),
            static_cast<int64_t>(test_string[r]));  // x in tf_test is {1, 2, 3, 4, 5}
      }
    }
  }
}

TEST_F(TableFunctions, ThrowingTests) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM "
                                    "TABLE(column_list_safe_row_sum(cursor(SELECT x FROM "
                                    "err_test)));",
                                    dt),
                   UserTableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM "
                                    "TABLE(column_list_safe_row_sum(cursor(SELECT y FROM "
                                    "err_test)));",
                                    dt),
                   UserTableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM "
                                    "TABLE(column_list_safe_row_sum(cursor(SELECT f FROM "
                                    "err_test)));",
                                    dt),
                   UserTableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM "
                                    "TABLE(column_list_safe_row_sum(cursor(SELECT d FROM "
                                    "err_test)));",
                                    dt),
                   UserTableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require(cursor(SELECT x"
                                    " FROM tf_test), -2));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_templating(cursor(SELECT x"
                           " FROM tf_test), -2));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_templating(cursor(SELECT d"
                           " FROM tf_test), -2));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require_and(cursor(SELECT x"
                                    " FROM tf_test), -2));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_or_str(cursor(SELECT x"
                           " FROM tf_test), 'string'));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_or_str(cursor(SELECT x"
                           " FROM tf_test), 'MI'));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_str_diff(cursor(SELECT x"
                           " FROM tf_test), 'MAX'));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_text_enc_dict(cursor(SELECT t2"
                           " FROM sd_test), 0));",
                           dt),
          TableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg(
                       "SELECT * FROM "
                       "TABLE(ct_require_text_collist_enc_dict(cursor(SELECT t2, t2, t2"
                       " FROM sd_test), 0));",
                       dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_require_cursor(cursor(SELECT x"
                           " FROM tf_test), 0));",
                           dt),
          TableFunctionError);
    }
    {
      if (dt == ExecutorDeviceType::GPU) {
        EXPECT_THROW(
            run_multiple_agg("SELECT * FROM TABLE(ct_require_device_cuda(cursor(SELECT x"
                             " FROM tf_test), -2));",
                             dt),
            TableFunctionError);
      }
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require_range(cursor(SELECT x"
                                    " FROM tf_test), 0, 1));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require_mgr(cursor(SELECT x"
                                    " FROM tf_test), -2));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(ct_require_mgr(cursor(SELECT x"
                                    " FROM tf_test), 6));",
                                    dt),
                   TableFunctionError);
    }
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_test_preflight_sizer(cursor(SELECT x"
                           " FROM tf_test), -2, -3));",
                           dt),
          TableFunctionError);
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(column_list_safe_row_sum(cursor(SELECT x2 "
          "FROM "
          "err_test)));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]),
                static_cast<int32_t>(10));  // 0+1+2+3+4=10
    }

    // Ensure TableFunctionMgr and error throwing works properly for templated
    // CPU TFs
    {
      EXPECT_THROW(
          run_multiple_agg("SELECT * FROM TABLE(ct_throw_if_gt_100(CURSOR(SELECT "
                           "CAST(f AS FLOAT) AS "
                           "f FROM (VALUES (0.0), (1.0), (2.0), (110.0)) AS t(f))));",
                           dt),
          UserTableFunctionError);
    }

    // These should throw directly from within UDTF code, be caught by the server, and
    // re-thrown as UserTableFunctionErrors
    if (dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d "
                                    "FROM tf_test), -3));",
                                    dt),
                   UserTableFunctionError);
    }

    if (dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d "
                                    "FROM tf_test), -4));",
                                    dt),
                   UserTableFunctionError);
    }

    if (dt == ExecutorDeviceType::CPU) {
      EXPECT_THROW(run_multiple_agg("SELECT out0 FROM TABLE(row_copier2(cursor(SELECT d "
                                    "FROM tf_test), -5));",
                                    dt),
                   UserTableFunctionError);
    }

    {
      const auto rows = run_multiple_agg(
          "SELECT CAST(val AS INT) AS val FROM "
          "TABLE(ct_throw_if_gt_100(CURSOR(SELECT "
          "CAST(f AS DOUBLE) AS f FROM (VALUES (0.0), (1.0), "
          "(2.0), (3.0)) AS t(f)))) "
          "ORDER BY val;",
          dt);
      const size_t num_rows = rows->rowCount();
      ASSERT_EQ(num_rows, size_t(4));
      for (size_t r = 0; r < num_rows; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int32_t>(r));
      }
    }
  }
}

std::string gen_grid_values(const int32_t num_x_bins,
                            const int32_t num_y_bins,
                            const bool add_w_val,
                            const bool merge_with_w_null,
                            const std::string& aliased_table = "") {
  const std::string project_w_sql = add_w_val ? ", CAST(w AS BIGINT) as w" : "";
  std::string values_sql =
      "SELECT CAST(id AS BIGINT) as id, CAST(x AS BIGINT) AS x, "
      "CAST(y AS BIGINT) AS y, CAST(z AS "
      "BIGINT) AS z" +
      project_w_sql + " FROM (VALUES ";
  auto gen_values = [](const int32_t num_x_bins,
                       const int32_t num_y_bins,
                       const int32_t start_id,
                       const int32_t start_x_bin,
                       const int32_t start_y_bin,
                       const bool add_w_val,
                       const bool w_is_null) {
    std::string values_sql;
    int32_t out_id = start_id;
    for (int32_t y_bin = start_y_bin; y_bin < start_y_bin + num_y_bins; ++y_bin) {
      for (int32_t x_bin = start_x_bin; x_bin < start_x_bin + num_x_bins; ++x_bin) {
        const std::string id_str = std::to_string(out_id);
        const std::string x_val_str = std::to_string(x_bin);
        const std::string y_val_str = std::to_string(y_bin);
        const int32_t z_val = x_bin * y_bin;
        const std::string z_val_str = std::to_string(z_val);
        const int32_t w_val = x_bin;
        const std::string w_val_str = w_is_null ? "null" : std::to_string(w_val);
        if (out_id++ > start_id) {
          values_sql += ", ";
        }
        values_sql +=
            "(" + id_str + ", " + x_val_str + ", " + y_val_str + ", " + z_val_str;
        if (add_w_val) {
          values_sql += ", " + w_val_str;
        }
        values_sql += ")";
      }
    }
    return values_sql;
  };
  if (add_w_val) {
    if (merge_with_w_null) {
      values_sql += gen_values(num_x_bins, num_y_bins, 0, 0, 0, true, true) + ", ";
      values_sql += gen_values(num_x_bins,
                               num_y_bins,
                               num_x_bins * num_y_bins,
                               num_x_bins,
                               num_y_bins,
                               true,
                               false);
    } else {
      values_sql += gen_values(num_x_bins,
                               num_y_bins,
                               num_x_bins * num_y_bins,
                               num_x_bins,
                               num_y_bins,
                               true,
                               false);
    }
  } else {
    values_sql += gen_values(num_x_bins, num_y_bins, 0, 0, 0, false, false);
  }
  if (aliased_table.empty()) {
    values_sql += ") AS t(id, x, y, z";
    if (add_w_val) {
      values_sql += ", w";
    }
    values_sql += ")";
  } else {
    values_sql += ") AS " + aliased_table;
  }
  return values_sql;
}

void check_result_set_equality(const ResultSetPtr rows_1, const ResultSetPtr rows_2) {
  const size_t num_result_rows_1 = rows_1->rowCount();
  const size_t num_result_rows_2 = rows_2->rowCount();
  ASSERT_EQ(num_result_rows_1, num_result_rows_2);
  const size_t num_result_cols_1 = rows_1->colCount();
  const size_t num_result_cols_2 = rows_2->colCount();
  ASSERT_EQ(num_result_cols_1, num_result_cols_2);
  for (size_t r = 0; r < num_result_rows_1; ++r) {
    const auto& row_1 = rows_1->getNextRow(false, false);
    const auto& row_2 = rows_2->getNextRow(false, false);
    for (size_t c = 0; c < num_result_cols_1; ++c) {
      ASSERT_EQ(TestHelpers::v<int64_t>(row_1[c]), TestHelpers::v<int64_t>(row_2[c]));
    }
  }
}

template <typename T>
void check_result_against_expected_result(
    const ResultSetPtr rows,
    const std::vector<std::vector<T>>& expected_result) {
  const size_t num_result_rows = rows->rowCount();
  ASSERT_EQ(num_result_rows, expected_result.size());
  const size_t num_result_cols = rows->colCount();
  for (size_t r = 0; r < num_result_rows; ++r) {
    const auto& expected_result_row = expected_result[r];
    const auto& row = rows->getNextRow(false, false);
    ASSERT_EQ(num_result_cols, expected_result_row.size());
    ASSERT_EQ(num_result_cols, row.size());
    for (size_t c = 0; c < num_result_cols; ++c) {
      ASSERT_EQ(TestHelpers::v<T>(row[c]), expected_result_row[c]);
    }
  }
}

void print_result(const ResultSetPtr rows) {
  const size_t num_result_rows = rows->rowCount();
  const size_t num_result_cols = rows->colCount();
  for (size_t r = 0; r < num_result_rows; ++r) {
    std::cout << std::endl << "Row: " << r << std::endl;
    const auto& row = rows->getNextRow(false, false);
    for (size_t c = 0; c < num_result_cols; ++c) {
      std::cout << "Col: " << c << " Result: " << TestHelpers::v<int64_t>(row[c])
                << std::endl;
    }
  }
}

TEST_F(TableFunctions, TimestampTests) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // extracting TIMESTAMP columns which already have nanosecond precision
    {
      const auto result = run_multiple_agg(
          "SELECT ns,us,ms,s,m,h,d,mo,y FROM "
          "TABLE(ct_timestamp_extract(cursor(SELECT t1 FROM "
          "time_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(3));
      std::vector<std::vector<int64_t>> expected_result_set;
      expected_result_set.emplace_back(
          std::vector<int64_t>({1001001001, 1001001, 1001, 1, 1, 1, 1, 1, 1971}));
      expected_result_set.emplace_back(
          std::vector<int64_t>({2002002002, 2002002, 2002, 2, 2, 2, 2, 2, 1972}));
      expected_result_set.emplace_back(
          std::vector<int64_t>({3003003003, 3003003, 3003, 3, 3, 3, 3, 3, 1973}));
      check_result_against_expected_result(result, expected_result_set);
    }
    // truncating TIMESTAMP columns which already have nanosecond precision
    {
      const auto result = run_multiple_agg(
          "SELECT us,ms,s,m,h,d,mo,y FROM "
          "TABLE(ct_timestamp_truncate(cursor(SELECT t1 FROM "
          "time_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(3));
      std::vector<std::vector<int64_t>> expected_result_set;
      std::vector<int64_t> vec1(result->colCount()), vec2(result->colCount()),
          vec3(result->colCount());
      std::vector<NullableString> vec_str1 =
          std::vector<NullableString>({"1971-01-01 01:01:01.001001000",
                                       "1971-01-01 01:01:01.001000000",
                                       "1971-01-01 01:01:01.000000000",
                                       "1971-01-01 01:01:00.000000000",
                                       "1971-01-01 01:00:00.000000000",
                                       "1971-01-01 00:00:00.000000000",
                                       "1971-01-01 00:00:00.000000000",
                                       "1971-01-01 00:00:00.000000000"});
      std::vector<NullableString> vec_str2 =
          std::vector<NullableString>({"1972-02-02 02:02:02.002002000",
                                       "1972-02-02 02:02:02.002000000",
                                       "1972-02-02 02:02:02.000000000",
                                       "1972-02-02 02:02:00.000000000",
                                       "1972-02-02 02:00:00.000000000",
                                       "1972-02-02 00:00:00.000000000",
                                       "1972-02-01 00:00:00.000000000",
                                       "1972-01-01 00:00:00.000000000"});
      std::vector<NullableString> vec_str3 =
          std::vector<NullableString>({"1973-03-03 03:03:03.003003000",
                                       "1973-03-03 03:03:03.003000000",
                                       "1973-03-03 03:03:03.000000000",
                                       "1973-03-03 03:03:00.000000000",
                                       "1973-03-03 03:00:00.000000000",
                                       "1973-03-03 00:00:00.000000000",
                                       "1973-03-01 00:00:00.000000000",
                                       "1973-01-01 00:00:00.000000000"});
      std::transform(
          vec_str1.begin(), vec_str1.end(), vec1.begin(), [](NullableString str) {
            return dateTimeParse<kTIMESTAMP>(boost::lexical_cast<std::string>(str), 9);
          });
      std::transform(
          vec_str2.begin(), vec_str2.end(), vec2.begin(), [](NullableString str) {
            return dateTimeParse<kTIMESTAMP>(boost::lexical_cast<std::string>(str), 9);
          });
      std::transform(
          vec_str3.begin(), vec_str3.end(), vec3.begin(), [](NullableString str) {
            return dateTimeParse<kTIMESTAMP>(boost::lexical_cast<std::string>(str), 9);
          });
      expected_result_set.push_back(vec1);
      expected_result_set.push_back(vec2);
      expected_result_set.push_back(vec3);
      check_result_against_expected_result(result, expected_result_set);
    }

    // TIMESTAMP columns which require upcasting to nanosecond precision
    {
      const auto result = run_multiple_agg(
          "SELECT ns,us,ms,s,m,h,d,mo,y FROM "
          "TABLE(ct_timestamp_extract(cursor(SELECT t1 FROM "
          "time_test_castable)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(3));
      std::vector<std::vector<int64_t>> expected_result_set;
      expected_result_set.emplace_back(
          std::vector<int64_t>({1001001000, 1001001, 1001, 1, 1, 1, 1, 1, 1971}));
      expected_result_set.emplace_back(
          std::vector<int64_t>({2002002000, 2002002, 2002, 2, 2, 2, 2, 2, 1972}));
      expected_result_set.emplace_back(
          std::vector<int64_t>({3003003000, 3003003, 3003, 3, 3, 3, 3, 3, 1973}));
      check_result_against_expected_result(result, expected_result_set);
    }

    // TIMESTAMP columns which cannot be upcasted to nanosecond precision, due to not
    // enough range
    {
      EXPECT_THROW(run_multiple_agg("SELECT ns,us,ms,s,m,h,d,mo,y FROM "
                                    "TABLE(ct_timestamp_extract(cursor(SELECT t1 "
                                    "FROM time_test_uncastable)));",
                                    dt),
                   std::runtime_error);
    }

    // TIMESTAMP scalar inputs
    {
      const auto result = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_timestamp_add_offset(cursor(SELECT t1 FROM "
          "time_test), TIMESTAMP(9) '1970-01-01 00:00:00.000000001'));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(3));
      std::vector<std::string> expected_result_set({"1971-01-01 01:01:01.001001002",
                                                    "1972-02-02 02:02:02.002002003",
                                                    "1973-03-03 03:03:03.003003004"});
      for (size_t i = 0; i < 3; i++) {
        auto row = result->getNextRow(true, false);
        Datum d;
        d.bigintval = TestHelpers::v<int64_t>(row[0]);
        std::string asString = DatumToString(d, SQLTypeInfo(kTIMESTAMP, 9, 0, false));
        ASSERT_EQ(asString, expected_result_set[i]);
      }
    }
    // Reproducer of QE-374
    if (dt == ExecutorDeviceType::GPU) {
      for (int i = 0; i < 2; i++) {
        // According to QE-374, the following query will hang forever
        // when i==1 due to unclean code cache locking:
        const auto result = run_multiple_agg(
            "SELECT * FROM TABLE(ct_timestamp_add_offset_cpu_only(cursor(SELECT t1 from"
            " time_test_castable), CAST('1970-01-01 00:00:00.000000001' AS "
            "TIMESTAMP(9))));",
            dt);
        ASSERT_EQ(result->rowCount(), size_t(1));
      }
    }

    // TIMESTAMP columns mixed with scalar inputs and sizer argument
    {
      const auto result = run_multiple_agg(
          "SELECT out0 FROM TABLE(ct_timestamp_test_columns_and_scalars("
          "cursor(SELECT t1 from time_test_castable), 100, cursor(SELECT t1 from "
          "time_test_castable)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(3));
      // expected: col + col + 100 nanoseconds
      std::vector<std::string> expected_result_set({"1972-01-01 02:02:02.002002100",
                                                    "1974-03-05 04:04:04.004004100",
                                                    "1976-05-03 06:06:06.006006100"});
      for (size_t i = 0; i < 3; i++) {
        auto row = result->getNextRow(true, false);
        Datum d;
        d.bigintval = TestHelpers::v<int64_t>(row[0]);
        std::string asString = DatumToString(d, SQLTypeInfo(kTIMESTAMP, 9, 0, false));
        ASSERT_EQ(asString, expected_result_set[i]);
      }
    }
  }
}

TEST_F(TableFunctions, FilterTransposeRuleOneCursor) {
  // Test FILTER_TABLE_FUNCTION_TRANSPOSE optimization on single cursor table
  // functions

  enum StatType { MIN, MAX };

  auto compare_tf_pushdown_with_values_rollup =
      [&](const std::string& values_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const StatType stat_type,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string stat_type_agg = stat_type == StatType::MIN ? "MIN" : "MAX";
        const std::string tf_query = "SELECT * FROM TABLE(ct_pushdown_stats('" +
                                     stat_type_agg + "', CURSOR(" + values_sql +
                                     "))) WHERE " + tf_filter + ";";
        std::string values_rollup_query =
            "SELECT COUNT(*) AS row_count, " + stat_type_agg + "(id) AS id, " +
            stat_type_agg + "(x) AS x, " + stat_type_agg + "(y) AS y, " + stat_type_agg +
            "(z) AS z FROM (" + values_sql + " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_rollup_query = "SELECT * FROM (" + values_rollup_query + ") WHERE " +
                                non_pushdown_filter_sql + ";";
        } else {
          values_rollup_query += ";";
        }
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_rollup_rows = run_multiple_agg(values_rollup_query, dt);
        check_result_set_equality(tf_rows, values_rollup_rows);
      };

  auto compare_tf_pushdown_with_values_projection =
      [&](const std::string& values_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string tf_query =
            "SELECT * FROM TABLE(ct_pushdown_projection(CURSOR(" + values_sql +
            "))) WHERE " + tf_filter + " ORDER BY id ASC;";

        std::string values_projection_query =
            "SELECT * FROM (" + values_sql + " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_projection_query = "SELECT * FROM (" + values_projection_query +
                                    ") WHERE " + non_pushdown_filter_sql;
        }
        values_projection_query += " ORDER BY id ASC;";
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_projection_rows = run_multiple_agg(values_projection_query, dt);
        check_result_set_equality(tf_rows, values_projection_rows);
      };

  auto run_tests_for_filter = [&](const std::string& values_sql,
                                  const std::string& filter_sql,
                                  const std::string& non_pushdown_filter_sql,
                                  const ExecutorDeviceType dt) {
    compare_tf_pushdown_with_values_rollup(
        values_sql, filter_sql, non_pushdown_filter_sql, StatType::MIN, dt);
    compare_tf_pushdown_with_values_rollup(
        values_sql, filter_sql, non_pushdown_filter_sql, StatType::MAX, dt);
    compare_tf_pushdown_with_values_projection(
        values_sql, filter_sql, non_pushdown_filter_sql, dt);
  };

  const std::string grid_values =
      gen_grid_values(8, 8, false /* add_w_val */, false /* merge_with_w_null */);
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Single cursor arguments

    {
      // no filter
      const std::string pushdown_filter{"TRUE"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // single filter
      const std::string pushdown_filter{"x <= 4"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // two filters
      const std::string pushdown_filter{"x < 4 AND y < 3"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // two filters - with betweens and equality
      const std::string pushdown_filter{"x BETWEEN 2 AND 4 AND y = 4"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // three filters - with inequality
      const std::string pushdown_filter{"z <> 6 AND x <= 3 AND y between -5 and 2"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // filter that filters out all rows
      // compare_tf_pushdown_with_values_rollup(grid_values, "z <> 3 AND x <= 0
      // AND y between 1 and 2", "", StatType::MIN, dt);
      const std::string pushdown_filter{"z <> 3 AND x <= 0 AND y between 1 and 2"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // four filters
      const std::string pushdown_filter{
          "z <> 3 AND x > 1 AND y between 1 and 4 AND id < 15"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // four pushdown filters + one filter that cannot be pushed down
      const std::string pushdown_filter{
          "z <> 3 AND x > 1 AND y between 1 and 8 AND id < 28"};
      const std::string non_pushdown_filter{"row_count > 0"};
      compare_tf_pushdown_with_values_rollup(
          grid_values, pushdown_filter, non_pushdown_filter, StatType::MIN, dt);
      compare_tf_pushdown_with_values_rollup(
          grid_values, pushdown_filter, non_pushdown_filter, StatType::MAX, dt);
    }

    {
      // disjunctive pushdown filter
      const std::string pushdown_filter{"x >= 2 OR y >= 3"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // conjunctive pushdown filter with disjunctive sub-predicate
      const std::string pushdown_filter{"x >= 1 OR y < 3 AND z < 4"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }

    {
      // More complexity...
      const std::string pushdown_filter{
          "x >= 1 AND x + y + z < 20 AND x * y < y + 6 OR z > 12"};
      run_tests_for_filter(grid_values, pushdown_filter, "", dt);
    }
  }
}

TEST_F(TableFunctions, FilterTransposeRuleMultipleCursors) {
  enum StatType { MIN, MAX };

  auto compare_tf_pushdown_with_values_rollup =
      [&](const std::string& values1_sql,
          const std::string& values2_sql,
          const std::string& values_merged_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const StatType stat_type,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string stat_type_agg = stat_type == StatType::MIN ? "MIN" : "MAX";
        const std::string tf_query = "SELECT * FROM TABLE(ct_union_pushdown_stats('" +
                                     stat_type_agg + "', CURSOR(" + values1_sql +
                                     "), CURSOR(" + values2_sql + "))) WHERE " +
                                     tf_filter + ";";

        std::string values_rollup_query =
            "SELECT COUNT(*) AS row_count, " + stat_type_agg + "(id) AS id, " +
            stat_type_agg + "(x) AS x, " + stat_type_agg + "(y) AS y, " + stat_type_agg +
            "(z) AS z, " + stat_type_agg + "(w) AS w FROM (" + values_merged_sql +
            " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_rollup_query = "SELECT * FROM (" + values_rollup_query + ") WHERE " +
                                non_pushdown_filter_sql + ";";
        } else {
          values_rollup_query += ";";
        }
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_rollup_rows = run_multiple_agg(values_rollup_query, dt);
        check_result_set_equality(tf_rows, values_rollup_rows);
      };

  auto compare_tf_pushdown_with_values_projection =
      [&](const std::string& values1_sql,
          const std::string& values2_sql,
          const std::string& values_merged_sql,
          const std::string& filter_sql,
          const std::string& non_pushdown_filter_sql,
          const ExecutorDeviceType dt) {
        std::string tf_filter = filter_sql;
        if (non_pushdown_filter_sql.size()) {
          tf_filter += " AND " + non_pushdown_filter_sql;
        }
        const std::string tf_query =
            "SELECT * FROM TABLE(ct_union_pushdown_projection(CURSOR(" + values1_sql +
            "), CURSOR(" + values2_sql + "))) WHERE " + tf_filter + " ORDER BY id;";

        std::string values_projection_query =
            "SELECT * FROM (" + values_merged_sql + " WHERE " + filter_sql + ")";
        if (!non_pushdown_filter_sql.empty()) {
          values_projection_query = "SELECT * FROM (" + values_projection_query +
                                    ") WHERE " + non_pushdown_filter_sql;
        }
        values_projection_query += " ORDER BY id ASC;";
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        const auto values_projection_rows = run_multiple_agg(values_projection_query, dt);
        check_result_set_equality(tf_rows, values_projection_rows);
      };

  // TM: Commenting out the following function to eliminate an unused variable
  // warning but leaving as its quite useful for debugging

  // auto print_tf_pushdown_projection = [&](const std::string& values1_sql,
  //                                        const std::string& values2_sql,
  //                                        const std::string& filter_sql,
  //                                        const std::string&
  //                                        non_pushdown_filter_sql, const
  //                                        ExecutorDeviceType dt) {
  //  std::string tf_filter = filter_sql;
  //  if (non_pushdown_filter_sql.size()) {
  //    tf_filter += " AND " + non_pushdown_filter_sql;
  //  }
  //  const std::string tf_query =
  //      "SELECT * FROM TABLE(ct_union_pushdown_projection(CURSOR(" +
  //      values1_sql +
  //      "), CURSOR(" + values2_sql + "))) WHERE " + tf_filter + " ORDER BY
  //      id;";
  //  const auto tf_rows = run_multiple_agg(tf_query, dt);
  //  print_result(tf_rows);
  //};

  auto compare_tf_pushdown_with_expected_result =
      [&](const std::string& values1_sql,
          const std::string& values2_sql,
          const std::string& filter_sql,
          const std::vector<std::vector<int64_t>>& expected_result,
          const StatType stat_type,
          const ExecutorDeviceType dt) {
        const std::string stat_type_agg = stat_type == StatType::MIN ? "MIN" : "MAX";
        const std::string tf_query = "SELECT * FROM TABLE(ct_union_pushdown_stats('" +
                                     stat_type_agg + "', CURSOR(" + values1_sql +
                                     "), CURSOR(" + values2_sql + "))) WHERE " +
                                     filter_sql + ";";
        const auto tf_rows = run_multiple_agg(tf_query, dt);
        check_result_against_expected_result(tf_rows, expected_result);
      };

  auto run_tests_for_filter = [&](const std::string& values1_sql,
                                  const std::string& values2_sql,
                                  const std::string& values_merged_sql,
                                  const std::string& filter_sql,
                                  const std::string& non_pushdown_filter_sql,
                                  const ExecutorDeviceType dt) {
    compare_tf_pushdown_with_values_rollup(values1_sql,
                                           values2_sql,
                                           values_merged_sql,
                                           filter_sql,
                                           non_pushdown_filter_sql,
                                           StatType::MIN,
                                           dt);
    compare_tf_pushdown_with_values_rollup(values1_sql,
                                           values2_sql,
                                           values_merged_sql,
                                           filter_sql,
                                           non_pushdown_filter_sql,
                                           StatType::MAX,
                                           dt);
    compare_tf_pushdown_with_values_projection(values1_sql,
                                               values2_sql,
                                               values_merged_sql,
                                               filter_sql,
                                               non_pushdown_filter_sql,
                                               dt);
  };

  const std::string grid_values_1 = gen_grid_values(8, 8, false, false);
  const std::string grid_values_2 = gen_grid_values(8, 8, true, false);
  const std::string grid_values_merged = gen_grid_values(8, 8, true, true);
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      // No filter
      const std::string filter_sql{"TRUE"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // One filter
      const std::string filter_sql{"x > 1"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // One range filter
      const std::string filter_sql{"x BETWEEN 1 AND 10"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // Two filters
      const std::string filter_sql{"x BETWEEN 4 AND 10 AND y < 9"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // Two filters - order swap
      const std::string filter_sql{"y < 9 AND x BETWEEN 4 AND 10"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // Three filters
      const std::string filter_sql{"x < 10 AND y > 4 AND z BETWEEN 4 AND 20"};
      run_tests_for_filter(
          grid_values_1, grid_values_2, grid_values_merged, filter_sql, "", dt);
    }

    {
      // One filter - push down only to one input (w)
      compare_tf_pushdown_with_expected_result(grid_values_1,
                                               grid_values_2,
                                               "w >= 12",
                                               {{96, 0, 0, 0, 0, 12}},
                                               StatType::MIN,
                                               dt);
      compare_tf_pushdown_with_expected_result(grid_values_1,
                                               grid_values_2,
                                               "w >= 12",
                                               {{96, 127, 15, 15, 225, 15}},
                                               StatType::MAX,
                                               dt);
      compare_tf_pushdown_with_values_projection(grid_values_1,
                                                 grid_values_2,
                                                 grid_values_merged,
                                                 "w >= 12 OR w IS null",
                                                 "",
                                                 dt);
    }

    {
      // Three filters - one only pushes down to one input (w)
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 7, 7, 0, 0, 8}},
          StatType::MIN,
          dt);
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 72, 9, 9, 72, 9}},
          StatType::MAX,
          dt);
      compare_tf_pushdown_with_values_projection(grid_values_1,
                                                 grid_values_2,
                                                 grid_values_merged,
                                                 "w BETWEEN 7 AND 10 OR w IS NULL",
                                                 "",
                                                 dt);
    }

    {
      // Three filters - one only pushes down to one input (w)
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 7, 7, 0, 0, 8}},
          StatType::MIN,
          dt);
      compare_tf_pushdown_with_expected_result(
          grid_values_1,
          grid_values_2,
          "z <= 72 AND w BETWEEN 7 AND 10 AND x >= 7",
          {{11, 72, 9, 9, 72, 9}},
          StatType::MAX,
          dt);
      compare_tf_pushdown_with_values_projection(grid_values_1,
                                                 grid_values_2,
                                                 grid_values_merged,
                                                 "w BETWEEN 7 AND 10 OR w IS NULL",
                                                 "",
                                                 dt);
    }
  }
}

TEST_F(TableFunctions, FilterTransposeRuleMisc) {
  // Test FILTER_TABLE_FUNCTION_TRANSPOSE optimization.

  auto check_result = [](const auto rows, std::vector<int64_t> result) {
    ASSERT_EQ(rows->rowCount(), result.size());
    for (size_t i = 0; i < result.size(); i++) {
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), result[i]);
    }
  };

  auto check_result2 =
      [](const auto rows, std::vector<int64_t> result1, std::vector<int64_t> result2) {
        ASSERT_EQ(rows->rowCount(), result1.size());
        ASSERT_EQ(rows->rowCount(), result2.size());
        for (size_t i = 0; i < result1.size(); i++) {
          auto crt_row = rows->getNextRow(false, false);
          ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), result1[i]);
          ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[1]), result2[i]);
        }
      };

  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_copy_and_add_size(cursor(SELECT x FROM "
          "tf_test WHERE "
          "x>1)));",
          dt);
      check_result(rows, {2 + 3, 3 + 3, 4 + 3});
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_copy_and_add_size(cursor(SELECT x FROM "
          "tf_test)))"
          "WHERE x>1;",
          dt);
      check_result(rows, {2 + 3, 3 + 3, 4 + 3});
    }
    {
      run_ddl_statement("DROP VIEW IF EXISTS view_ct_copy_and_add_size");
      run_ddl_statement(
          "CREATE VIEW view_ct_copy_and_add_size AS SELECT * FROM "
          "TABLE(ct_copy_and_add_size(cursor(SELECT x FROM tf_test)));");
      const auto rows1 =
          run_multiple_agg("SELECT * FROM view_ct_copy_and_add_size WHERE x>1;", dt);
      check_result(rows1, {2 + 3, 3 + 3, 4 + 3});
      const auto rows2 = run_multiple_agg("SELECT * FROM view_ct_copy_and_add_size;", dt);
      check_result(rows2, {0 + 5, 1 + 5, 2 + 5, 3 + 5, 4 + 5});
    }
    {
      // x=0,1,2,3,4
      // x2=5,4,3,2,1
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_add_size_and_mul_alpha(cursor(SELECT x, x2 "
          "FROM "
          "tf_test WHERE "
          "x>1 and x2>1), 4));",
          dt);
      check_result2(rows, {2 + 2, 3 + 2}, {3 * 4, 2 * 4});
    }
    {
      // x =0,1,2,3,4
      // x2=5,4,3,2,1
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ct_add_size_and_mul_alpha(cursor(SELECT x, x2 "
          "FROM "
          "tf_test), 4)) WHERE x>1 and x2>1;",
          dt);
      check_result2(rows, {2 + 2, 3 + 2}, {3 * 4, 2 * 4});
    }
    // Multiple cursor arguments
    {
      const auto rows = run_multiple_agg(
          "SELECT x, d FROM TABLE(ct_sparse_add(cursor(SELECT "
          "x, x FROM tf_test), 0"
          ", cursor(SELECT x, x FROM tf_test), 0));",
          dt);
      check_result2(
          rows, {0, 1, 2, 3, 4}, {0, (1 + 1) * 5, (2 + 2) * 5, (3 + 3) * 5, (4 + 4) * 5});
    }
    {
      const auto rows = run_multiple_agg(
          "SELECT x, d FROM TABLE(ct_sparse_add(cursor(SELECT "
          "x, x FROM tf_test), 0"
          ", cursor(SELECT x, x + 1 FROM tf_test WHERE x > "
          "2), 15)) WHERE (x > 1 AND x < "
          "4);",
          dt);
      check_result2(rows, {2, 3}, {(2 + 15) * 2, (3 + 4) * 2});
    }
  }
}

TEST_F(TableFunctions, ResultsetRecycling) {
  auto executor = Executor::getExecutor(Executor::UNITARY_EXECUTOR_ID).get();
  auto clearCache = [&executor] {
    executor->clearMemory(MemoryLevel::CPU_LEVEL);
    executor->getQueryPlanDagCache().clearQueryPlanCache();
  };
  clearCache();

  ScopeGuard reset_global_flag_state =
      [orig_resulset_recycler = g_use_query_resultset_cache,
       orig_data_recycler = g_enable_data_recycler,
       orig_chunk_metadata_recycler = g_use_chunk_metadata_cache] {
        g_use_query_resultset_cache = orig_resulset_recycler;
        g_enable_data_recycler = orig_data_recycler;
        g_use_chunk_metadata_cache = orig_chunk_metadata_recycler;
      };
  g_enable_data_recycler = true;
  g_use_query_resultset_cache = true;
  g_use_chunk_metadata_cache = true;

  // put resultset to cache in advance
  auto q1 =
      "SELECT /*+ keep_table_function_result */ out0 FROM TABLE(row_copier(cursor(SELECT "
      "d FROM tf_test), 1)) ORDER BY out0;";
  auto q2 =
      "SELECT /*+ keep_table_function_result */ out0 FROM "
      "TABLE(sort_column_limit(CURSOR(SELECT x FROM tf_test), 3, false, true)) ORDER by "
      "out0 DESC;";
  auto q3 =
      "SELECT /*+ keep_table_function_result */ out0 FROM "
      "TABLE(ct_binding_column2(cursor(SELECT d FROM tf_test), cursor(SELECT x from "
      "tf_test)));";
  auto q4 =
      "SELECT /*+ keep_table_function_result */ total FROM "
      "TABLE(ct_named_rowmul_output(cursor(SELECT x FROM tf_test), 2));";
  auto q5 =
      "SELECT /*+ keep_table_function_result */ answer FROM "
      "TABLE(ct_no_arg_constant_sizing()) ORDER BY answer;";
  auto q6 =
      "SELECT /*+ keep_table_function_result */ output FROM "
      "TABLE(ct_templated_no_cursor_user_constant_sizer(7, 4));";
  run_multiple_agg(q1, ExecutorDeviceType::CPU);
  run_multiple_agg(q2, ExecutorDeviceType::CPU);
  run_multiple_agg(q3, ExecutorDeviceType::CPU);
  run_multiple_agg(q4, ExecutorDeviceType::CPU);
  run_multiple_agg(q5, ExecutorDeviceType::CPU);
  run_multiple_agg(q6, ExecutorDeviceType::CPU);

  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(q1, dt);
      ASSERT_EQ(rows->rowCount(), size_t(5));
    }

    {
      const auto rows = run_multiple_agg(q2, dt);
      ASSERT_EQ(rows->rowCount(), size_t(3));
      std::vector<int64_t> expected_result_set{4, 3, 2};
      for (size_t i = 0; i < 3; i++) {
        auto row = rows->getNextRow(true, false);
        auto v = TestHelpers::v<int64_t>(row[0]);
        ASSERT_EQ(v, expected_result_set[i]);
      }
    }

    {
      const auto rows = run_multiple_agg(q3, dt);
      ASSERT_EQ(rows->rowCount(), size_t(1));
      auto crt_row = rows->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(40));
    }

    {
      const auto rows = run_multiple_agg(q4, dt);
      ASSERT_EQ(rows->rowCount(), size_t(10));
    }

    {
      const auto rows = run_multiple_agg(q5, dt);
      ASSERT_EQ(rows->rowCount(), size_t(42));
      for (size_t i = 0; i < 42; i++) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(42 * i));
      }
    }

    {
      const auto rows = run_multiple_agg(q6, dt);
      ASSERT_EQ(rows->rowCount(), size_t(4));

      for (size_t r = 0; r < 4; ++r) {
        auto crt_row = rows->getNextRow(false, false);
        ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(7));
      }
    }
  }
}

struct Point {
  double x, y;
  std::string toString() const {
    return "{" + std::to_string(x) + "," + std::to_string(y) + "}";
  }
};
bool comparePoint(Point& p1, Point& p2) {
  double dx = p1.x - p2.x;
  if (std::abs(dx) < 1e-7) {
    return p1.y < p2.y;
  }
  return dx < 0;
}

template <typename T>
void assert_equal(const TargetValue& val1,
                  const TargetValue& val2,
                  const SQLTypeInfo& ti) {
  if (ti.is_integer() || ti.is_boolean() || ti.is_text_encoding_dict()) {
    const auto scalar_col_val1 = boost::get<ScalarTargetValue>(&val1);
    const auto scalar_col_val2 = boost::get<ScalarTargetValue>(&val2);
    auto p1 = boost::get<int64_t>(scalar_col_val1);
    auto p2 = boost::get<int64_t>(scalar_col_val2);
    ASSERT_EQ(static_cast<T>(*p1), static_cast<T>(*p2)) << "ti=" << ti.to_string();
  } else if (ti.is_fp()) {
    const auto scalar_col_val1 = boost::get<ScalarTargetValue>(&val1);
    const auto scalar_col_val2 = boost::get<ScalarTargetValue>(&val2);
    if constexpr (std::is_same_v<T, float>) {
      auto p1 = boost::get<float>(scalar_col_val1);
      auto p2 = boost::get<float>(scalar_col_val2);
      ASSERT_EQ(static_cast<T>(*p1), static_cast<T>(*p2)) << "ti=" << ti.to_string();
    } else {
      if constexpr (!std::is_same_v<T, double>) {
        UNREACHABLE();
      }
      auto p1 = boost::get<double>(scalar_col_val1);
      auto p2 = boost::get<double>(scalar_col_val2);
      ASSERT_NEAR(static_cast<T>(*p1), static_cast<T>(*p2), 1e-7)
          << "ti=" << ti.to_string();
    }
  } else if (ti.is_text_encoding_none()) {
    const auto scalar_col_val1 = boost::get<ScalarTargetValue>(&val1);
    const auto scalar_col_val2 = boost::get<ScalarTargetValue>(&val2);
    const auto ns1 = boost::get<NullableString>(scalar_col_val1);
    const auto ns2 = boost::get<NullableString>(scalar_col_val2);
    const auto s1 = boost::get<std::string>(ns1);
    const auto s2 = boost::get<std::string>(ns2);
    const std::string nullstr = "";
    if (s1 == nullptr) {
      if (s2 != nullptr) {
        ASSERT_EQ(*s2, nullstr);
      }
    } else if (s2 == nullptr) {
      if (s1 != nullptr) {
        ASSERT_EQ(*s1, nullstr);
      }
    } else {
      ASSERT_EQ(*s1, *s2);
    }
  } else if (ti.is_array()) {
    const auto array_col_val1 = boost::get<ArrayTargetValue>(&val1);
    const auto array_col_val2 = boost::get<ArrayTargetValue>(&val2);
    if (array_col_val1->is_initialized()) {
      if (array_col_val2->is_initialized()) {
        const auto& vec1 = array_col_val1->get();
        const auto& vec2 = array_col_val2->get();
        ASSERT_EQ(vec1.size(), vec2.size()) << "ti=" << ti.to_string();
        for (size_t i = 0; i < vec1.size(); i++) {
          const auto item1 = vec1[i];
          const auto item2 = vec2[i];
          assert_equal<T>(item1, item2, ti.get_elem_type());
        }
      } else {
        const auto& vec1 = array_col_val1->get();
        ASSERT_EQ(vec1.size(), size_t(0)) << "ti=" << ti.to_string();
      }
    } else {
      // null array
      if (array_col_val2->is_initialized()) {
        const auto& vec2 = array_col_val2->get();
        ASSERT_EQ(vec2.size(), size_t(0)) << "ti=" << ti.to_string();
      }
    }
  } else if (ti.is_geometry()) {
    const auto scalar_col_val1 = boost::get<ScalarTargetValue>(&val1);
    const auto scalar_col_val2 = boost::get<ScalarTargetValue>(&val2);
    const auto ns1 = boost::get<NullableString>(scalar_col_val1);
    const auto ns2 = boost::get<NullableString>(scalar_col_val2);
    const auto s1 = boost::get<std::string>(ns1);
    const auto s2 = boost::get<std::string>(ns2);
    const std::string nullstr = "NULL";
    if (s1 == nullptr) {
      if (s2 != nullptr) {
        ASSERT_EQ(*s2, nullstr);
      }
    } else if (s2 == nullptr) {
      if (s1 != nullptr) {
        ASSERT_EQ(*s1, nullstr);
      }
    } else {
      std::vector<double> coords1, coords2;
      std::vector<double> bounds1, bounds2;
      std::vector<int32_t> ring_sizes1, ring_sizes2;
      std::vector<int32_t> poly_sizes1, poly_sizes2;
      switch (ti.get_type()) {
        case kLINESTRING: {
          const auto gdal_wkt_ls1 = Geospatial::GeoLineString(*s1);
          gdal_wkt_ls1.getColumns(coords1, bounds1);
          ring_sizes1.push_back(coords1.size() / 2);
          const auto gdal_wkt_ls2 = Geospatial::GeoLineString(*s2);
          gdal_wkt_ls2.getColumns(coords2, bounds2);
          ring_sizes2.push_back(coords2.size() / 2);
          break;
        }
        case kPOLYGON: {
          const auto gdal_wkt_poly1 = Geospatial::GeoPolygon(*s1);
          gdal_wkt_poly1.getColumns(coords1, ring_sizes1, bounds1);
          const auto gdal_wkt_poly2 = Geospatial::GeoPolygon(*s2);
          gdal_wkt_poly2.getColumns(coords2, ring_sizes2, bounds2);
          break;
        }
        case kMULTIPOINT: {
          const auto gdal_wkt_ls1 = Geospatial::GeoMultiPoint(*s1);
          gdal_wkt_ls1.getColumns(coords1, bounds1);
          ring_sizes1.push_back(coords1.size() / 2);
          const auto gdal_wkt_ls2 = Geospatial::GeoMultiPoint(*s2);
          gdal_wkt_ls2.getColumns(coords2, bounds2);
          ring_sizes2.push_back(coords2.size() / 2);
          break;
        }
        case kMULTILINESTRING: {
          const auto gdal_wkt_poly1 = Geospatial::GeoMultiLineString(*s1);
          gdal_wkt_poly1.getColumns(coords1, ring_sizes1, bounds1);
          const auto gdal_wkt_poly2 = Geospatial::GeoMultiLineString(*s2);
          gdal_wkt_poly2.getColumns(coords2, ring_sizes2, bounds2);
          break;
        }
        case kMULTIPOLYGON: {
          const auto gdal_wkt_mpoly1 = Geospatial::GeoMultiPolygon(*s1);
          gdal_wkt_mpoly1.getColumns(coords1, ring_sizes1, poly_sizes1, bounds1);
          const auto gdal_wkt_mpoly2 = Geospatial::GeoMultiPolygon(*s2);
          gdal_wkt_mpoly2.getColumns(coords2, ring_sizes2, poly_sizes2, bounds2);
          break;
        }
        default:
          UNREACHABLE() << "ti=" << ti.to_string();
      }
      ASSERT_EQ(poly_sizes1.size(), poly_sizes2.size());
      for (size_t i = 0; i < poly_sizes1.size(); i++) {
        ASSERT_EQ(poly_sizes1[i], poly_sizes2[i]);
      }
      ASSERT_EQ(ring_sizes1.size(), ring_sizes2.size());
      ASSERT_EQ(coords1.size(), coords2.size());
      int64_t k = 0;
      for (size_t i = 0; i < ring_sizes1.size(); i++) {
        ASSERT_EQ(ring_sizes1[i], ring_sizes2[i]);
        std::vector<Point> points1;
        std::vector<Point> points2;
        int64_t sz = ring_sizes1[i];
        for (int32_t j = 0; j < sz; j++) {
          Point p1{coords1[k], coords1[k + 1]};
          Point p2{coords2[k], coords2[k + 1]};
          points1.push_back(p1);
          points2.push_back(p2);
          k += 2;
        }
        std::sort(points1.begin(), points1.end(), comparePoint);
        std::sort(points2.begin(), points2.end(), comparePoint);
        for (int32_t j = 0; j < sz; j++) {
          Point p1 = points1[j];
          Point p2 = points2[j];
          ASSERT_NEAR(p1.x, p2.x, 1e-6);
          ASSERT_NEAR(p1.y, p2.y, 1e-6);
        }
      }
    }
  } else {
    UNREACHABLE() << "ti=" << ti.to_string();
  }
}

template <typename T>
void assert_equal(const ResultSetPtr rows1, const ResultSetPtr rows2) {
  const size_t num_rows1 = rows1->rowCount();
  ASSERT_EQ(num_rows1, rows2->rowCount());
  const size_t num_cols1 = rows1->colCount();
  ASSERT_EQ(num_cols1, rows2->colCount());
  for (size_t r = 0; r < num_rows1; ++r) {
    const auto& row1 = rows1->getRowAtNoTranslations(r);
    const auto& row2 = rows2->getRowAtNoTranslations(r);
    ASSERT_EQ(row1.size(), row2.size());
    for (size_t c = 0; c < num_cols1; ++c) {
      if (c >= row1.size()) {
        ASSERT_EQ(row1.size(), size_t(0));
        break;
      }
      const auto ti1 = rows1->getColType(c);
      const auto ti2 = rows2->getColType(c);
      if ((ti1.is_text_encoding_dict() && ti2.is_text_encoding_dict()) &&
          ti1.getStringDictKey() != ti2.getStringDictKey()) {
        auto proxy1 = rows1->getStringDictionaryProxy(ti1.getStringDictKey());
        auto proxy2 = rows2->getStringDictionaryProxy(ti2.getStringDictKey());
        const TargetValue val1 = row1[c];
        const TargetValue val2 = row2[c];
        const auto scalar_col_val1 = boost::get<ScalarTargetValue>(&val1);
        const auto scalar_col_val2 = boost::get<ScalarTargetValue>(&val2);
        auto p1 = boost::get<int64_t>(scalar_col_val1);
        auto p2 = boost::get<int64_t>(scalar_col_val2);
        std::string s1 = proxy1->getString(static_cast<int32_t>(*p1));
        std::string s2 = proxy2->getString(static_cast<int32_t>(*p2));
        ASSERT_EQ(s1, s2);
      } else if (ti1.is_text_encoding_dict_array() && ti2.is_text_encoding_dict_array() &&
                 ti1.getStringDictKey() != ti2.getStringDictKey()) {
        auto proxy1 = rows1->getStringDictionaryProxy(ti1.getStringDictKey());
        auto proxy2 = rows2->getStringDictionaryProxy(ti2.getStringDictKey());
        const TargetValue val1 = row1[c];
        const TargetValue val2 = row2[c];
        const auto array_col_val1 = boost::get<ArrayTargetValue>(&val1);
        const auto array_col_val2 = boost::get<ArrayTargetValue>(&val2);
        if (array_col_val1->is_initialized()) {
          if (array_col_val2->is_initialized()) {
            const auto& vec1 = array_col_val1->get();
            const auto& vec2 = array_col_val2->get();
            ASSERT_EQ(vec1.size(), vec2.size());
            for (size_t i = 0; i < vec1.size(); i++) {
              const auto item1 = vec1[i];
              const auto item2 = vec2[i];
              auto p1 = boost::get<int64_t>(item1);
              auto p2 = boost::get<int64_t>(item2);
              std::string s1 = proxy1->getString(static_cast<int32_t>(p1));
              std::string s2 = proxy2->getString(static_cast<int32_t>(p2));
              ASSERT_EQ(s1, s2);
            }
          } else {
            const auto& vec1 = array_col_val1->get();
            ASSERT_EQ(vec1.size(), size_t(0));
          }
        } else {
          if (array_col_val2->is_initialized()) {
            const auto& vec2 = array_col_val2->get();
            ASSERT_EQ(vec2.size(), size_t(0));
          }
        }
      } else {
        ASSERT_EQ(ti1, ti2) << "  ti1=" << ti1.toString() << "\n  ti2=" << ti2.toString();
        const TargetValue item1 = row1[c];
        const TargetValue item2 = row2[c];
        assert_equal<T>(item1, item2, ti1);
      }
    }
  }
}

TEST_F(TableFunctions, CalciteOverloading) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // Calcite should pick up the correct overload based on the input scalar's type,
    // and then build the WHERE clause filter correctly. If Calcite does not choose the
    // overload properly, then the query fails to parse due to a type mismatch between the
    // output type of the table function vs the WHERE clause's operand
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_scalar_test(10)) WHERE out0 = 10;", dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
      auto result_row = result->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(result_row[0]), static_cast<int64_t>(10));
    }
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_scalar_test(TIMESTAMP(9) '1970-01-01 "
          "00:00:00.000000000')) WHERE out0 = '1970-01-01 00:00:00.000000000';",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
      auto result_row = result->getNextRow(false, false);
      int64_t as_int64 = dateTimeParse<kTIMESTAMP>("1970-01-01 00:00:00", 9);
      ASSERT_EQ(TestHelpers::v<int64_t>(result_row[0]), as_int64);
    }
    // Calcite should pick up the correct overload based on the input column's subtype,
    // and then build the WHERE clause filter correctly. If Calcite does not choose the
    // overload properly, then the query fails to parse due to a type mismatch between the
    // output type of the table function vs the WHERE clause's operand
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_test(CURSOR(SELECT y from "
          "err_test))) WHERE out0 = 0;",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(0));
    }
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_test(CURSOR(SELECT t1 from "
          "sd_test))) WHERE out0 = 'California';",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
      auto result_row = result->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(result_row[0]), static_cast<int64_t>(0));
    }
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_test(CURSOR(SELECT t1 from "
          "time_test))) WHERE out0 = '1971-01-01 01:01:01.001001001';",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(0));
    }
    // Query should succeed, two BIGINT columns with a COLUMN_LIST of one single DOUBLE
    // column in between.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR("
          "SELECT y, d, y FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should succeed. Three BIGINT columns should match as one preceding column,
    // one column list of a single BIGINT column, then one last BIGINT column.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR("
          "SELECT y, y, y FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should succeed, two BIGINT columns with a COLUMN_LIST of three DOUBLE
    // columns in between.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR("
          "SELECT y, d, d, d, y FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should fail, missing a last BIGINT input column after the column list of
    // DOUBLEs
    {
      EXPECT_ANY_THROW(
          run_multiple_agg("SELECT out0 from table(ct_overload_column_list_test(CURSOR("
                           "SELECT y, d, d FROM err_test)));",
                           dt));
    }
    // Query should succeed, two BIGINT columns followed by two DOUBLE columns means
    // the two middle columns are each a column list of a single column
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test2(CURSOR("
          "SELECT y, y, d, d FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should succeed, two column lists of three elements each in between single
    // columns of BIGINT and DOUBLE types
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test2(CURSOR("
          "SELECT y, y, y, y, d, d, d, d FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should fail, first column and column list arguments should be BIGINT
    // columns not DOUBLE
    {
      EXPECT_ANY_THROW(
          run_multiple_agg("SELECT out0 from table(ct_overload_column_list_test2(CURSOR("
                           "SELECT d, d, d, d FROM err_test)));",
                           dt));
    }
  }
}

TEST_F(TableFunctions, CalciteAutoCasting) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    // These tests were taken from the 'CalciteOverloading' tests. Before auto-casting
    // from Calcite was available, these would fail due to type mismatches. With
    // auto-casting, these queries become valid since input types can be promoted. Query
    // should succeed, first column is BIGINT. The second BIGINT column can be promoted to
    // DOUBLE and be aggregated in a COLUMNLIST with the third DOUBLE argument. The last
    // BIGINT column matches normally.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR(SELECT y, y, d, y "
          "FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should succeed, first column is BIGINT. The second to last BIGINT column can
    // be promoted to DOUBLE and be aggregated in the COLUMNLIST. Last BIGINT column
    // matches normally.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR("
          "SELECT y, d, d, d, y, y FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should succeed, the first INT column can be promoted to BIGINT. The rest
    // match normally.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR("
          "SELECT x, d, d, y FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should succeed, the last INT column can be promoted to BIGINT. The rest match
    // normally.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR("
          "SELECT y, d, d, x FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Query should succeed, first and last BIGINT columns match. The two intermediate
    // FLOAT columns can be promoted to DOUBLE and match with the DOUBLE COLUMNLIST
    // argument.
    {
      const auto result = run_multiple_agg(
          "SELECT out0 from table(ct_overload_column_list_test(CURSOR("
          "SELECT x, f, f, x FROM err_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
    }
    // Tests for promoting smaller integer types to BIGINT
    if (dt == ExecutorDeviceType::CPU) {
      {
        const auto result_tinyint = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_bigint(CURSOR(SELECT CAST (x "
            "AS TINYINT) from tf_test)));",
            dt);
        const auto result_smallint = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_bigint(CURSOR(SELECT CAST (x "
            "AS SMALLINT) from tf_test)));",
            dt);
        const auto result_integer = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_bigint(CURSOR(SELECT CAST (x "
            "AS INTEGER) from tf_test)));",
            dt);
        const auto result_bigint =
            run_multiple_agg("SELECT CAST(x as BIGINT) from tf_test;", dt);
        assert_equal<int64_t>(result_tinyint, result_bigint);
        assert_equal<int64_t>(result_smallint, result_bigint);
        assert_equal<int64_t>(result_integer, result_bigint);
      }
      {
        const auto result_float = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_double(CURSOR(SELECT f from "
            "tf_test)));",
            dt);
        const auto result_tinyint = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_double(CURSOR(SELECT cast (x "
            "as TINYINT) from tf_test)));",
            dt);
        const auto result_smallint = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_double(CURSOR(SELECT cast (x "
            "as SMALLINT) from tf_test)));",
            dt);
        const auto result_integer = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_double(CURSOR(SELECT cast (x "
            "as INTEGER) from tf_test)));",
            dt);
        const auto result_bigint = run_multiple_agg(
            "SELECT out0 FROM table(ct_test_calcite_casting_double(CURSOR(SELECT cast (x "
            "as BIGINT) from tf_test)));",
            dt);
        const auto result_int_to_double =
            run_multiple_agg("SELECT CAST (x as DOUBLE) from tf_test;", dt);
        const auto result_double =
            run_multiple_agg("SELECT CAST(f as DOUBLE) from tf_test;", dt);
        assert_equal<double>(result_tinyint, result_int_to_double);
        assert_equal<double>(result_smallint, result_int_to_double);
        assert_equal<double>(result_integer, result_int_to_double);
        assert_equal<double>(result_bigint, result_int_to_double);
        assert_equal<double>(result_float, result_double);
      }
    }
    // TODO: Re-enable cast to string type tests after ColumnList binding is resolved.
    if (false) {
      const auto result_float = run_multiple_agg(
          "SELECT out0 FROM table(ct_test_calcite_casting_char(CURSOR(SELECT f from "
          "tf_test)));",
          dt);
      const auto result_integer = run_multiple_agg(
          "SELECT out0 FROM table(ct_test_calcite_casting_char(CURSOR(SELECT x from "
          "tf_test)));",
          dt);
      const auto result_timestamp = run_multiple_agg(
          "SELECT out0 FROM table(ct_test_calcite_casting_char(CURSOR(SELECT t1 from "
          "time_test)));",
          dt);
      const auto result_float_as_varchar =
          run_multiple_agg("SELECT CAST (f as VARCHAR) from tf_test;", dt);
      const auto result_int_as_varchar =
          run_multiple_agg("SELECT CAST (x as VARCHAR) from tf_test;", dt);
      const auto result_timestamp_as_varchar =
          run_multiple_agg("SELECT CAST(t1 as VARCHAR) from time_test;", dt);
      for (int i = 0; i < 3; ++i) {
        auto float_row = result_float->getNextRow(true, false);
        auto integer_row = result_integer->getNextRow(true, false);
        auto timestamp_row = result_timestamp->getNextRow(true, false);
        auto float_as_varchar_row = result_float_as_varchar->getNextRow(true, false);
        auto int_as_varchar_row = result_int_as_varchar->getNextRow(true, false);
        auto timestamp_as_varchar = result_timestamp_as_varchar->getNextRow(true, false);
        auto string_float =
            boost::get<std::string>(TestHelpers::v<NullableString>(float_row[0]));
        auto string_integer =
            boost::get<std::string>(TestHelpers::v<NullableString>(integer_row[0]));
        auto string_timestamp =
            boost::get<std::string>(TestHelpers::v<NullableString>(timestamp_row[0]));
        auto string_expected_float = boost::get<std::string>(
            TestHelpers::v<NullableString>(float_as_varchar_row[0]));
        auto string_expected_integer = boost::get<std::string>(
            TestHelpers::v<NullableString>(int_as_varchar_row[0]));
        auto string_expected_timestamp = boost::get<std::string>(
            TestHelpers::v<NullableString>(timestamp_as_varchar[0]));
        ASSERT_EQ(string_float, string_expected_float);
        ASSERT_EQ(string_integer, string_expected_integer);
        ASSERT_EQ(string_timestamp, string_expected_timestamp);
      }
    }
    {
      const auto result_timestamp = run_multiple_agg(
          "SELECT out0 FROM table(ct_test_calcite_casting_timestamp(CURSOR(SELECT t1 "
          "from time_test_castable)));",
          dt);
      std::vector<std::string> expected_result_set({"1971-01-01 01:01:01.001001000",
                                                    "1972-02-02 02:02:02.002002000",
                                                    "1973-03-03 03:03:03.003003000"});
      for (int i = 0; i < 3; ++i) {
        const auto row = result_timestamp->getNextRow(true, false);
        Datum d;
        d.bigintval = TestHelpers::v<int64_t>(row[0]);
        std::string asString = DatumToString(d, SQLTypeInfo(kTIMESTAMP, 9, 0, false));
        ASSERT_EQ(asString, expected_result_set[i]);
      }
    }
    // test for QE-724
    // casting algorithm should realize that casting the single FLOAT column to DOUBLE is
    // the only option, as DATEDIFF returns a DOUBLE, which cannot be downcasted to FLOAT
    {
      const auto result_qe724 = run_multiple_agg(
          "SELECT out0 FROM table(ct_test_calcite_casting_columnlist(data => "
          "CURSOR(SELECT f, datediff('month', nsTimestamp(0), nsTimestamp(0)) / 12.0 "
          "from tf_test)));",
          dt);
      ASSERT_EQ(result_qe724->rowCount(), size_t(2));
    }

    // tests for QE-788
    // Calcite will consider result of some binary ops involving literals (such as FLOAT +
    // literal) as a wider type (such as DOUBLE), even though they are still passed to the
    // backend as FLOAT. The casting algorithm should cast operands of such ops to the
    // wider type explicitly, so that they typecheck.
    {
      const auto result_qe788_column = run_multiple_agg(
          "SELECT out0 from table(row_copier(CURSOR(select f + 1.0 from tf_test)));", dt);
      const auto result_qe788_column_perm = run_multiple_agg(
          "SELECT out0 from table(row_copier(CURSOR(select 1.0 + f from tf_test)));", dt);
      const auto expected_result_qe788_column = run_multiple_agg(
          "SELECT out0 from table(row_copier(CURSOR(select f + CAST(1.0 as DOUBLE) from "
          "tf_test)));",
          dt);
      assert_equal<double>(result_qe788_column, expected_result_qe788_column);
      assert_equal<double>(result_qe788_column_perm, expected_result_qe788_column);
    }
    {
      const auto result_qe788_columnlist = run_multiple_agg(
          "SELECT out0 from table(row_copier_columnlist(CURSOR(select f + 1.0, f + 2.0 "
          "from tf_test)));",
          dt);
      const auto result_qe788_columnlist_perm = run_multiple_agg(
          "SELECT out0 from table(row_copier_columnlist(CURSOR(select 1.0 + f, 2.0 + f "
          "from tf_test)));",
          dt);
      const auto expected_result_qe788_columnlist = run_multiple_agg(
          "SELECT out0 from table(row_copier_columnlist(cols => CURSOR(select f + "
          "CAST(1.0 as DOUBLE), f + CAST(2.0 as DOUBLE) from tf_test)));",
          dt);
      assert_equal<double>(result_qe788_columnlist, expected_result_qe788_columnlist);
      assert_equal<double>(result_qe788_columnlist_perm,
                           expected_result_qe788_columnlist);
    }
  }
}

TEST_F(TableFunctions, ColumnArraySumAlongRow) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
#define COLUMNARRAYSUMALONGROWTEST(COLNAME, CTYPE)                                      \
  {                                                                                     \
    const auto rows =                                                                   \
        run_multiple_agg("SELECT out0 FROM TABLE(sum_along_row(cursor(SELECT " #COLNAME \
                         " FROM arr_test)));",                                          \
                         dt);                                                           \
    const auto result_rows =                                                            \
        run_multiple_agg("SELECT sum_along_row_" #COLNAME " FROM arr_test;", dt);       \
    assert_equal<CTYPE>(rows, result_rows);                                             \
  }
    COLUMNARRAYSUMALONGROWTEST(barr, bool);
    COLUMNARRAYSUMALONGROWTEST(carr, int8_t);
    COLUMNARRAYSUMALONGROWTEST(sarr, int16_t);
    COLUMNARRAYSUMALONGROWTEST(iarr, int32_t);
    COLUMNARRAYSUMALONGROWTEST(larr, int64_t);
    COLUMNARRAYSUMALONGROWTEST(farr, float);
    COLUMNARRAYSUMALONGROWTEST(darr, double);
    COLUMNARRAYSUMALONGROWTEST(tarr, int32_t);
  }
}

TEST_F(TableFunctions, ColumnArrayCopier) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
#define COLUMNARRAYCOPIERTEST(COLNAME, CTYPE)                                            \
  {                                                                                      \
    const auto rows =                                                                    \
        run_multiple_agg("SELECT out0 FROM TABLE(array_copier(cursor(SELECT " #COLNAME   \
                         " FROM arr_test)));",                                           \
                         dt);                                                            \
    const auto result_rows = run_multiple_agg("SELECT " #COLNAME " FROM arr_test;", dt); \
    assert_equal<CTYPE>(rows, result_rows);                                              \
  }
    COLUMNARRAYCOPIERTEST(barr, bool);
    COLUMNARRAYCOPIERTEST(carr, int8_t);
    COLUMNARRAYCOPIERTEST(sarr, int16_t);
    COLUMNARRAYCOPIERTEST(iarr, int32_t);
    COLUMNARRAYCOPIERTEST(larr, int64_t);
    COLUMNARRAYCOPIERTEST(farr, float);
    COLUMNARRAYCOPIERTEST(darr, double);
    COLUMNARRAYCOPIERTEST(tarr, int32_t);
  }
}

TEST_F(TableFunctions, ColumnArrayAt) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
#define COLUMNARRAYATTEST(COLNAME, CTYPE, SUFFIX)                                  \
  {                                                                                \
    std::string q1 =                                                               \
        "SELECT (out0)" #SUFFIX " FROM TABLE(array_copier(cursor(SELECT " #COLNAME \
        " FROM arr_test)));";                                                      \
    const auto rows = run_multiple_agg(q1, dt);                                    \
    std::string q2 = "SELECT (" #COLNAME ")" #SUFFIX " FROM arr_test;";            \
    const auto result_rows = run_multiple_agg(q1, dt);                             \
    assert_equal<CTYPE>(rows, result_rows);                                        \
  }
    COLUMNARRAYATTEST(barr, bool, [1]);
    COLUMNARRAYATTEST(carr, int8_t, [1]);
    COLUMNARRAYATTEST(sarr, int16_t, [1]);
    COLUMNARRAYATTEST(iarr, int32_t, [1]);
    COLUMNARRAYATTEST(larr, int64_t, [1]);
    COLUMNARRAYATTEST(farr, float, [1]);
    COLUMNARRAYATTEST(darr, double, [1]);
    COLUMNARRAYATTEST(tarr, int32_t, [1]);

    COLUMNARRAYATTEST(iarr, int32_t, [2]);

    COLUMNARRAYATTEST(carr, int8_t, [1] + 3);
    COLUMNARRAYATTEST(sarr, int16_t, [1] + 3);
    COLUMNARRAYATTEST(iarr, int32_t, [1] + 3);
    COLUMNARRAYATTEST(larr, int64_t, [1] + 3);
    COLUMNARRAYATTEST(farr, float, [1] + 3);
    COLUMNARRAYATTEST(darr, double, [1] + 3);
  }
}

TEST_F(TableFunctions, ColumnArrayUnnest) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
#define COLUMNARRAYUNNEST(COLNAME, CTYPE)                                             \
  {                                                                                   \
    std::string q1 =                                                                  \
        "SELECT UNNEST(out0) AS ua  FROM TABLE(array_copier(cursor(SELECT " #COLNAME  \
        " FROM arr_test))) GROUP BY ua;";                                             \
    const auto rows = run_multiple_agg(q1, dt);                                       \
    std::string q2 = "SELECT UNNEST(" #COLNAME ") AS ua  FROM arr_test GROUP BY ua;"; \
    const auto result_rows = run_multiple_agg(q2, dt);                                \
    assert_equal<CTYPE>(rows, result_rows);                                           \
  }

    COLUMNARRAYUNNEST(barr, bool);
    COLUMNARRAYUNNEST(carr, int8_t);
    COLUMNARRAYUNNEST(sarr, int16_t);
    COLUMNARRAYUNNEST(iarr, int32_t);
    COLUMNARRAYUNNEST(larr, int64_t);
    COLUMNARRAYUNNEST(farr, float);
    COLUMNARRAYUNNEST(darr, double);
    COLUMNARRAYUNNEST(tarr, int32_t);
  }
}

TEST_F(TableFunctions, ColumnArrayConcat) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
#define COLUMNARRAYCONCATTEST(COLNAME, CTYPE)                                          \
  {                                                                                    \
    const auto rows =                                                                  \
        run_multiple_agg("SELECT out0 FROM TABLE(array_concat(cursor(SELECT " #COLNAME \
                         ", " #COLNAME " FROM arr_test)));",                           \
                         dt);                                                          \
    const auto result_rows =                                                           \
        run_multiple_agg("SELECT concat_" #COLNAME " FROM arr_test;", dt);             \
    assert_equal<CTYPE>(rows, result_rows);                                            \
  }
    COLUMNARRAYCONCATTEST(barr, bool);
    COLUMNARRAYCONCATTEST(carr, int8_t);
    COLUMNARRAYCONCATTEST(sarr, int16_t);
    COLUMNARRAYCONCATTEST(iarr, int32_t);
    COLUMNARRAYCONCATTEST(larr, int64_t);
    COLUMNARRAYCONCATTEST(farr, float);
    COLUMNARRAYCONCATTEST(darr, double);
    COLUMNARRAYCONCATTEST(tarr, int32_t);
  }
}

TEST_F(TableFunctions, ColumnArrayAppendLiteralArray) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
#define COLUMNARRAYCONCATLITERALARRAYTEST(COLNAME, CTYPE, SQLTYPE)                     \
  {                                                                                    \
    const auto rows =                                                                  \
        run_multiple_agg("SELECT out0 FROM TABLE(array_append(cursor(SELECT " #COLNAME \
                         " FROM arr_test), ARRAY[CAST(NULL AS " #SQLTYPE               \
                         "), CAST(99 AS " #SQLTYPE "), CAST(88 AS " #SQLTYPE ")]));",  \
                         dt);                                                          \
    const auto result_rows =                                                           \
        run_multiple_agg("SELECT concat_" #COLNAME "_literal FROM arr_test;", dt);     \
    assert_equal<CTYPE>(rows, result_rows);                                            \
  }
    COLUMNARRAYCONCATLITERALARRAYTEST(carr, int8_t, TINYINT);
    COLUMNARRAYCONCATLITERALARRAYTEST(sarr, int16_t, SMALLINT);
    COLUMNARRAYCONCATLITERALARRAYTEST(iarr, int32_t, INT);
    COLUMNARRAYCONCATLITERALARRAYTEST(larr, int64_t, BIGINT);
    COLUMNARRAYCONCATLITERALARRAYTEST(farr, float, FLOAT);
    COLUMNARRAYCONCATLITERALARRAYTEST(darr, double, DOUBLE);
    {
      const auto rows = run_multiple_agg(
          "SELECT out0 FROM TABLE(array_append(cursor(SELECT barr FROM arr_test), "
          "ARRAY[]));",
          dt);
      const auto result_rows = run_multiple_agg("SELECT barr FROM arr_test;", dt);
      assert_equal<bool>(rows, result_rows);
    }
  }
}

TEST_F(TableFunctions, ColumnArrayCopierOneRow) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
#define COLUMNARRAYCOPIERTESTONEROW(COLNAME, CTYPE)                                    \
  {                                                                                    \
    const auto rows =                                                                  \
        run_multiple_agg("SELECT out0 FROM TABLE(array_copier(cursor(SELECT " #COLNAME \
                         " FROM arr_test WHERE rowid=0)));",                           \
                         dt);                                                          \
    const auto result_rows =                                                           \
        run_multiple_agg("SELECT " #COLNAME " FROM arr_test WHERE rowid=0;", dt);      \
    assert_equal<CTYPE>(rows, result_rows);                                            \
  }
    COLUMNARRAYCOPIERTESTONEROW(barr, bool);
    COLUMNARRAYCOPIERTESTONEROW(carr, int8_t);
    COLUMNARRAYCOPIERTESTONEROW(sarr, int16_t);
    COLUMNARRAYCOPIERTESTONEROW(iarr, int32_t);
    COLUMNARRAYCOPIERTESTONEROW(larr, int64_t);
    COLUMNARRAYCOPIERTESTONEROW(farr, float);
    COLUMNARRAYCOPIERTESTONEROW(darr, double);
    COLUMNARRAYCOPIERTESTONEROW(tarr, int32_t);
  }
}

TEST_F(TableFunctions, ColumnArrayAsArray) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
#define COLUMNARRAYASARRAYTEST(COLNAME, CTYPE)                                          \
  {                                                                                     \
    const auto rows =                                                                   \
        run_multiple_agg("SELECT out0 FROM TABLE(array_asarray(cursor(SELECT " #COLNAME \
                         " FROM arr_test)));",                                          \
                         dt);                                                           \
    const auto result_rows =                                                            \
        run_multiple_agg("SELECT asarray_" #COLNAME " FROM arr_test;", dt);             \
    assert_equal<CTYPE>(rows, result_rows);                                             \
  }
    COLUMNARRAYASARRAYTEST(l, int64_t);
    COLUMNARRAYASARRAYTEST(t, int32_t);
  }
}

TEST_F(TableFunctions, ColumnArraySplit) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
#define COLUMNARRAYSPLIT(COLNAME, CTYPE)                                      \
  {                                                                           \
    const auto result_rows =                                                  \
        run_multiple_agg("SELECT array_first_half(" #COLNAME                  \
                         "), array_second_half(" #COLNAME ") FROM arr_test;", \
                         dt);                                                 \
    const auto rows = run_multiple_agg(                                       \
        "SELECT out0, out1 FROM TABLE(array_split(cursor(SELECT " #COLNAME    \
        " FROM arr_test)));",                                                 \
        dt);                                                                  \
    assert_equal<CTYPE>(rows, result_rows);                                   \
  }
    COLUMNARRAYSPLIT(barr, bool);
    COLUMNARRAYSPLIT(carr, int8_t);
    COLUMNARRAYSPLIT(sarr, int16_t);
    COLUMNARRAYSPLIT(iarr, int32_t);
    COLUMNARRAYSPLIT(larr, int64_t);
    COLUMNARRAYSPLIT(farr, float);
    COLUMNARRAYSPLIT(darr, double);
    COLUMNARRAYSPLIT(tarr, int32_t);
  }
}

TEST_F(TableFunctions, MetadataSetGet) {
  // this should work
  EXPECT_NO_THROW(
      run_multiple_agg("SELECT * FROM TABLE(tf_metadata_getter(CURSOR(SELECT * FROM "
                       "TABLE(tf_metadata_setter()))));",
                       ExecutorDeviceType::CPU));

  // As of [HEV-82] we changed the metadata cache behavior
  // to allow metadata overwrites to support multiple calls to the
  // same table function within a query, meaning that the below
  // query which would have thrown, now should not
  EXPECT_NO_THROW(run_multiple_agg("SELECT * FROM TABLE(tf_metadata_setter_repeated());",
                                   ExecutorDeviceType::CPU));

  // these should throw
  EXPECT_THROW(run_multiple_agg("SELECT * FROM TABLE(tf_metadata_getter(CURSOR(SELECT "
                                "* FROM TABLE(tf_metadata_setter_size_mismatch()))));",
                                ExecutorDeviceType::CPU),
               UserTableFunctionError);

  EXPECT_THROW(
      run_multiple_agg("SELECT * FROM TABLE(tf_metadata_getter_bad(CURSOR(SELECT "
                       "* FROM TABLE(tf_metadata_setter()))));",
                       ExecutorDeviceType::CPU),
      UserTableFunctionError);
}

TEST_F(TableFunctions, ColumnArray20KLimit) {
  // See https://heavyai.atlassian.net/browse/QE-461
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    {  // row count below the 20K limit
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ARRAY_COPIER(CURSOR(SELECT {generate_series} FROM "
          "TABLE(generate_series(1, 500)))));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(500));
    }

    {  // row count above the 20K limit
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ARRAY_COPIER(CURSOR(SELECT {generate_series} FROM "
          "TABLE(generate_series(1, 30000)))));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(30000));
    }
  }
}

TEST_F(TableFunctions, ColumnGeoPoint) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col = "p" + std::to_string(i);
      {
        // Test Column<GeoPoint> input
        std::string q1 =
            "SELECT ST_X(" + col + "), ST_Y(" + col + ") FROM geo_point_test;";
        std::string q2 = "SELECT x, y FROM TABLE(CT_COORDS(CURSOR(SELECT " + col +
                         " FROM geo_point_test)));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
      {
        // Test Column<GeoPoint> input and output
        std::string q1 = "SELECT ST_X(" + col + ") + 1.5, ST_Y(" + col +
                         ") - 2.5 FROM geo_point_test;";
        std::string q2 =
            "SELECT x, y FROM TABLE(CT_COORDS(CURSOR(SELECT shifted FROM "
            "TABLE(CT_SHIFT(CURSOR(SELECT " +
            col + " FROM geo_point_test), 1.5, -2.5)))));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoLineStringOutput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string lcol = "l" + std::to_string(i);
      {
        // Test Column<GeoLineString> output
        std::string q1 = "SELECT ST_SETSRID(" + lcol + ", 0) FROM geo_point_test;";
        std::string q2 =
            "SELECT linestrings FROM TABLE(ct_make_linestring2(CURSOR(SELECT ST_X(" +
            pcol + "), ST_Y(" + pcol + ") FROM geo_point_test), 2, 3));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoLineStringInput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col = "l" + std::to_string(i);
      {
        // Test Column<GeoLineString> input
        std::string q1 = "SELECT ST_X(ST_POINTN(" + col + ", 1)), ST_Y(ST_POINTN(" + col +
                         ", 1)) FROM geo_line_string_test;";
        std::string q2 = "SELECT x, y FROM TABLE(CT_POINTN(CURSOR(SELECT " + col +
                         " FROM geo_line_string_test), 1));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoLineStringCopy) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col = "l" + std::to_string(i);
      {
        // Test Column<GeoLineString> input and output
        std::string q1 = "SELECT " + col + " FROM geo_line_string_test;";
        std::string q2 = "SELECT outputs FROM TABLE(CT_COPY(CURSOR(SELECT " + col +
                         " FROM geo_line_string_test)))";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiLineStringCopy) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col = "ml" + std::to_string(i);
      {
        // Test Column<GeoLineString> input and output
        std::string q1 = "SELECT " + col + " FROM geo_polygon_test;";
        std::string q2 = "SELECT outputs FROM TABLE(CT_COPY(CURSOR(SELECT " + col +
                         " FROM geo_polygon_test)))";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoPolygonCopy) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col = "p" + std::to_string(i);
      {
        // Test Column<GeoLineString> input and output
        std::string q1 = "SELECT " + col + " FROM geo_polygon_test;";
        std::string q2 = "SELECT outputs FROM TABLE(CT_COPY(CURSOR(SELECT " + col +
                         " FROM geo_polygon_test)))";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiPolygonCopy) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col = "mp" + std::to_string(i);
      {
        // Test Column<GeoLineString> input and output
        std::string q1 = "SELECT " + col + " FROM geo_polygon_test;";
        std::string q2 = "SELECT outputs FROM TABLE(CT_COPY(CURSOR(SELECT " + col +
                         " FROM geo_polygon_test)))";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoPolygonInput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string rcol = "r" + std::to_string(i);
      std::string hcol = "h" + std::to_string(i);
      std::string hhcol = "hh" + std::to_string(i);
      // Test Column<GeoPolygon> input
      {
        std::string q1 = "SELECT " + rcol + " FROM geo_polygon_test;";
        std::string q2 = "SELECT linestrings FROM TABLE(CT_LINESTRINGN(CURSOR(SELECT " +
                         pcol + " FROM geo_polygon_test), 1));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
      {
        std::string q1 = "SELECT " + hcol + " FROM geo_polygon_test;";
        std::string q2 = "SELECT linestrings FROM TABLE(CT_LINESTRINGN(CURSOR(SELECT " +
                         pcol + " FROM geo_polygon_test), 2));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
      {
        std::string q1 = "SELECT " + hhcol + " FROM geo_polygon_test;";
        std::string q2 = "SELECT linestrings FROM TABLE(CT_LINESTRINGN(CURSOR(SELECT " +
                         pcol + " FROM geo_polygon_test), 3));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoPolygonOutput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string rcol = "r" + std::to_string(i);
      std::string hcol = "h" + std::to_string(i);
      std::string hhcol = "hh" + std::to_string(i);
      {
        std::string q1 = "SELECT " + pcol + " FROM geo_polygon_test;";
        std::string q2 = "SELECT polygons FROM TABLE(CT_MAKE_POLYGON3(CURSOR(SELECT " +
                         rcol + ", " + hcol + ", " + hhcol + " FROM geo_polygon_test)));";

        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }

    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string rcol = "r" + std::to_string(i);
      std::string hcol = "h" + std::to_string(i);
      std::string hhcol = "hh" + std::to_string(i);
      {
        std::string q1 = "SELECT sizes as key, COUNT(*) FROM (SELECT " + pcol +
                         ", sizes FROM geo_polygon_test) GROUP BY key;";
        std::string q2 =
            "SELECT sizes as key, COUNT(*) FROM (SELECT polygons, sizes FROM "
            "TABLE(CT_MAKE_POLYGON3(CURSOR(SELECT " +
            rcol + ", " + hcol + ", " + hhcol +
            " FROM geo_polygon_test)))) GROUP BY key;";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoPolygonInOutput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string rcol = "r" + std::to_string(i);
      std::string hcol = "h" + std::to_string(i);
      std::string hhcol = "hh" + std::to_string(i);
      {
        std::string q1 = "SELECT " + rcol + " FROM geo_polygon_test;";
        std::string q2 =
            "SELECT linestrings FROM TABLE(CT_LINESTRINGN(CURSOR("
            "SELECT polygons FROM TABLE(CT_MAKE_POLYGON3(CURSOR(SELECT " +
            rcol + ", " + hcol + ", " + hhcol + " FROM geo_polygon_test)))), 1));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiLineStringOutput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string mlcol = "ml" + std::to_string(i);
      {
        std::string q1 = "SELECT " + mlcol + " FROM geo_polygon_test;";
        std::string q2 =
            "SELECT mlinestrings FROM TABLE(CT_TO_MULTILINESTRING(CURSOR(SELECT " + pcol +
            " FROM geo_polygon_test)));";

        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiLineStringInput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string mlcol = "ml" + std::to_string(i);
      {
        std::string q1 = "SELECT " + pcol + " FROM geo_polygon_test;";
        std::string q2 = "SELECT polygons FROM TABLE(CT_TO_POLYGON(CURSOR(SELECT " +
                         mlcol + " FROM geo_polygon_test)));";

        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiPolygonOutput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string mpcol = "mp" + std::to_string(i);
      // Test Column<GeoMultiPolygon> output
      {
        std::string q1 = "SELECT " + mpcol + " FROM geo_polygon_test;";
        std::string q2 =
            "SELECT mpolygons FROM TABLE(CT_MAKE_MULTIPOLYGON(CURSOR(SELECT " + pcol +
            " FROM geo_polygon_test)));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiPolygonInput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string mpcol = "mp" + std::to_string(i);
      // Test Column<GeoMultiPolygon> input
      {
        std::string q1 = "SELECT " + pcol + " FROM geo_polygon_test;";
        std::string q2 = "SELECT polygons FROM TABLE(CT_POLYGONN(CURSOR(SELECT " + mpcol +
                         " FROM geo_polygon_test), 1));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiPolygonInOutput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string pcol = "p" + std::to_string(i);
      std::string mpcol = "mp" + std::to_string(i);
      // Test Column<GeoPolygon> input, Column<GeoMultiPolygon>
      // output, Column<GeoMultiPolygon> input, Column<GeoPolygon>
      // output
      {
        std::string q1 = "SELECT " + pcol + " FROM geo_polygon_test;";
        std::string q2 =
            "SELECT polygons FROM TABLE(CT_POLYGONN(CURSOR(SELECT"
            " mpolygons FROM TABLE(CT_MAKE_MULTIPOLYGON(CURSOR(SELECT " +
            pcol + " FROM geo_polygon_test)))), 1));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
      // Test Column<GeoMultiPolygon> input, Column<GeoPolygon>
      // output, Column<GeoPolygon> input, Column<GeoMultiPolygon>
      // output:
      {
        std::string q1 = "SELECT " + mpcol + " FROM geo_polygon_test;";
        std::string q2 =
            "SELECT mpolygons FROM TABLE(CT_MAKE_MULTIPOLYGON("
            "CURSOR(SELECT polygons FROM TABLE(CT_POLYGONN(CURSOR(SELECT " +
            mpcol + " FROM geo_polygon_test), 1)))));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiPointInput) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col1 =
          "l" + std::to_string(i);  // ST_POINTN does not support multipoint
      std::string col = "mp" + std::to_string(i);
      {
        // Test Column<GeoMultiPoint> input
        std::string q1 = "SELECT ST_X(ST_POINTN(" + col1 + ", 1)), ST_Y(ST_POINTN(" +
                         col1 + ", 1)) FROM geo_line_string_test;";
        std::string q2 = "SELECT x, y FROM TABLE(CT_POINTN(CURSOR(SELECT " + col +
                         " FROM geo_line_string_test), 1));";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnGeoMultiPointCopy) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    for (int i = 1; i <= 4; i++) {
      std::string col = "mp" + std::to_string(i);
      {
        // Test Column<GeoMultiPoint> input and output
        std::string q1 = "SELECT " + col + " FROM geo_line_string_test;";
        std::string q2 = "SELECT outputs FROM TABLE(CT_COPY(CURSOR(SELECT " + col +
                         " FROM geo_line_string_test)))";
        const auto expected_rows = run_multiple_agg(q1, dt);
        const auto rows = run_multiple_agg(q2, dt);
        assert_equal<double>(rows, expected_rows);
      }
    }
  }
}

TEST_F(TableFunctions, ColumnTextEncodingNoneCopy) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    {
      std::string q1 = "SELECT t1 FROM bytes_test;";
      std::string q2 =
          "SELECT outputs FROM TABLE(CT_COPY(CURSOR(SELECT t1 FROM bytes_test)))";
      const auto expected_rows = run_multiple_agg(q1, dt);
      const auto rows = run_multiple_agg(q2, dt);
      assert_equal<double>(rows, expected_rows);
    }
  }
}

TEST_F(TableFunctions, ColumnTextEncodingNoneConcat) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    {
      std::string q1 = "SELECT t12 FROM bytes_test;";
      std::string q2 =
          "SELECT outputs FROM TABLE(CT_CONCAT(CURSOR(SELECT t1, t2 FROM bytes_test)))";
      const auto expected_rows = run_multiple_agg(q1, dt);
      const auto rows = run_multiple_agg(q2, dt);
      assert_equal<double>(rows, expected_rows);
    }
    {
      std::string q1 = "SELECT t1abc FROM bytes_test;";
      std::string q2 =
          "SELECT outputs FROM TABLE(CT_CONCAT(CURSOR(SELECT t1 FROM bytes_test), "
          "'abc'))";
      const auto expected_rows = run_multiple_agg(q1, dt);
      const auto rows = run_multiple_agg(q2, dt);
      assert_equal<double>(rows, expected_rows);
    }
    {
      std::string q1 = "SELECT abct1 FROM bytes_test;";
      std::string q2 =
          "SELECT outputs FROM TABLE(CT_CONCAT('abc', CURSOR(SELECT t1 FROM "
          "bytes_test)))";
      const auto expected_rows = run_multiple_agg(q1, dt);
      const auto rows = run_multiple_agg(q2, dt);
      assert_equal<double>(rows, expected_rows);
    }
  }
}

TEST_F(TableFunctions, DefaultScalarValues) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();
    std::string regular_query =
        "SELECT out0 from TABLE(ct_test_int_default_arg(inp0 => CURSOR(select x from "
        "tf_test), x => 10));";
    std::string default_arg_query =
        "SELECT out0 from TABLE(ct_test_int_default_arg(inp0 => CURSOR(select x from "
        "tf_test)));";
    auto expected_rows = run_multiple_agg(regular_query, dt);
    auto rows = run_multiple_agg(default_arg_query, dt);
    assert_equal<int32_t>(rows, expected_rows);

    regular_query =
        "SELECT out0 from TABLE(ct_test_int_default_arg(inp0 => CURSOR(select y from "
        "err_test), x => 10));";
    default_arg_query =
        "SELECT out0 from TABLE(ct_test_int_default_arg(inp0 => CURSOR(select y from "
        "err_test)));";
    expected_rows = run_multiple_agg(regular_query, dt);
    rows = run_multiple_agg(default_arg_query, dt);
    assert_equal<int64_t>(rows, expected_rows);

    regular_query =
        "SELECT out0 from TABLE(ct_test_float_default_arg(inp0 => CURSOR(select f from "
        "tf_test), x => 10.0));";
    default_arg_query =
        "SELECT out0 from TABLE(ct_test_float_default_arg(inp0 => CURSOR(select f from "
        "tf_test)));";
    expected_rows = run_multiple_agg(regular_query, dt);
    rows = run_multiple_agg(default_arg_query, dt);
    assert_equal<float>(rows, expected_rows);

    regular_query =
        "SELECT out0 from TABLE(ct_test_float_default_arg(inp0 => CURSOR(select d from "
        "tf_test), x => 10.0));";
    default_arg_query =
        "SELECT out0 from TABLE(ct_test_float_default_arg(inp0 => CURSOR(select d from "
        "tf_test)));";
    expected_rows = run_multiple_agg(regular_query, dt);
    rows = run_multiple_agg(default_arg_query, dt);
    assert_equal<double>(rows, expected_rows);
  }
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    std::string regular_query =
        "SELECT out0 from TABLE(ct_test_string_default_arg(inp0 => CURSOR(select t1 from "
        "sd_test), suffix => \'_suffix\'));";
    std::string default_arg_query =
        "SELECT out0 from TABLE(ct_test_string_default_arg(inp0 => CURSOR(select t1 from "
        "sd_test)));";
    const auto expected_rows = run_multiple_agg(regular_query, dt);
    const auto rows = run_multiple_agg(default_arg_query, dt);
    for (size_t i = 0; i < 5; ++i) {
      const auto expected_row = expected_rows->getNextRow(true, false);
      const auto default_query_row = rows->getNextRow(true, false);
      auto expected_string =
          boost::get<std::string>(TestHelpers::v<NullableString>(expected_row[0]));
      auto default_query_string =
          boost::get<std::string>(TestHelpers::v<NullableString>(default_query_row[0]));
      ASSERT_EQ(expected_string, default_query_string);
    }
  }
}

TEST_F(TableFunctions, TFBindingAlgorithm) {
  for (auto dt : {ExecutorDeviceType::CPU, ExecutorDeviceType::GPU}) {
    SKIP_NO_GPU();

    {
      const auto result = run_multiple_agg(
          "SELECT * from table(ct_binding_udtf_constant(CURSOR(SELECT l, l, l, l, l, "
          "l, "
          "l, l, l, l, l, l, l, l, l, l, l, l, l, l from arr_test)));",
          dt);
      ASSERT_EQ(result->rowCount(), size_t(1));
      auto crt_row = result->getNextRow(false, false);
      ASSERT_EQ(TestHelpers::v<int64_t>(crt_row[0]), static_cast<int64_t>(242));
    }
  }
}

#ifdef HAVE_RUNTIME_LIBS
TEST_F(TableFunctions, RuntimeLibTestTableFunctions) {
  {
    // Calling a template table function which uses functions from a library loaded at
    // runtime
    const auto result_double = run_multiple_agg(
        "SELECT out0 FROM TABLE(ct_test_runtime_libs_add(cursor(SELECT d FROM tf_test), "
        "cursor(SELECT d FROM tf_test)));",
        ExecutorDeviceType::CPU);
    const auto result_int = run_multiple_agg(
        "SELECT out0 FROM TABLE(ct_test_runtime_libs_add(cursor(SELECT x FROM tf_test), "
        "cursor(SELECT x FROM tf_test)));",
        ExecutorDeviceType::CPU);
    const auto expected_double =
        run_multiple_agg("SELECT d + d FROM tf_test", ExecutorDeviceType::CPU);
    const auto expected_int = run_multiple_agg(
        "SELECT CAST(x + x AS BIGINT) FROM tf_test", ExecutorDeviceType::CPU);
    assert_equal<double>(result_double, expected_double);
    assert_equal<int64_t>(result_int, expected_int);
  }
  {
    // Calling a template table function which uses template functions from a library
    // loaded at runtime
    const auto result_double = run_multiple_agg(
        "SELECT out0 FROM TABLE(ct_test_runtime_libs_sub(cursor(SELECT d FROM tf_test), "
        "cursor(SELECT d FROM tf_test)));",
        ExecutorDeviceType::CPU);
    const auto result_int = run_multiple_agg(
        "SELECT out0 FROM TABLE(ct_test_runtime_libs_sub(cursor(SELECT x FROM tf_test), "
        "cursor(SELECT x FROM tf_test)));",
        ExecutorDeviceType::CPU);
    const auto expected_double =
        run_multiple_agg("SELECT d - d FROM tf_test;", ExecutorDeviceType::CPU);
    const auto expected_int = run_multiple_agg(
        "SELECT CAST(x - x AS BIGINT) FROM tf_test;", ExecutorDeviceType::CPU);
    assert_equal<double>(result_double, expected_double);
    assert_equal<int64_t>(result_int, expected_int);
  }
}
#endif

TEST_F(TableFunctions, RowRepeater) {
  for (auto dt : {ExecutorDeviceType::CPU /*, ExecutorDeviceType::GPU*/}) {
    SKIP_NO_GPU();
    {
      const auto rows = run_multiple_agg(
          "SELECT * FROM TABLE(ROW_REPEATER(CURSOR(SELECT generate_series FROM "
          "TABLE(generate_series(1, 1000))), 30));",
          dt);
      ASSERT_EQ(rows->rowCount(), size_t(30000));
    }
  }
}

int main(int argc, char** argv) {
  TestHelpers::init_logger_stderr_only(argc, argv);
  testing::InitGoogleTest(&argc, argv);

  // Table function support must be enabled before initialized the query runner
  // environment
  g_enable_table_functions = true;
  g_enable_dev_table_functions = true;
  QR::init(BASE_PATH);

  int err{0};
  try {
    err = RUN_ALL_TESTS();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
  QR::reset();
  return err;
}
