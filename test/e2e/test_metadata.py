"""
元数据查询测试
"""

import sys
import os

import pyodbc

# 添加父目录到路径以便导入
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from base_test import BaseTest, print_section


class TestMetadata(BaseTest):
    """元数据测试类"""
    
    def test_sqltables(self):
        """测试 SQLTables - 获取表列表"""
        conn = self.connect()
        cursor = conn.cursor()
        
        # 获取所有表
        cursor.tables(tableType='TABLE')
        tables = cursor.fetchall()
        
        self.assert_greater(len(tables), 0, "Should return at least one table")
        
        # 检查返回的列
        for table in tables:
            self.assert_not_none(table[2], "Table name should not be None")
        
        conn.close()
    
    def test_sqlcolumns(self):
        """测试 SQLColumns - 获取列信息"""
        conn = self.connect()
        cursor = conn.cursor()

        # 获取 information_schema.tables 的列信息
        cursor.columns(table='information_schema.tables')
        columns = cursor.fetchall()

        self.assert_greater(len(columns), 0, "Should return at least one column")

        # 检查列信息结构, 收集列名 -> DATA_TYPE 映射 (col[3]=COLUMN_NAME, col[4]=DATA_TYPE)
        col_types = {}
        for col in columns:
            self.assert_not_none(col[3], "Column name should not be None")
            col_types[col[3].lower()] = col[4]

        # information_schema.tables.table_name 是 STRING 列, 现在应当上报为 SQL_WVARCHAR (-9)
        # 而不是过去的 SQL_VARCHAR (12). 这是 Power BI 中文显示修复的核心断言.
        if 'table_name' in col_types:
            self.assert_equals(col_types['table_name'], pyodbc.SQL_WVARCHAR,
                               "STRING column 'table_name' must report DATA_TYPE=SQL_WVARCHAR")

        conn.close()
    
    def test_sqldescribecol(self):
        """测试 SQLDescribeCol - 描述列属性"""
        conn = self.connect()
        cursor = conn.cursor()

        cursor.execute("SELECT 1 as num, 'test' as str, 3.14 as pi")

        # pyodbc uses cursor.description instead of cursor.describe()
        # description returns a list of tuples: (name, type_code, display_size, internal_size, precision, scale, nullable)
        desc = cursor.description
        self.assert_not_none(desc, "Cursor description should not be None")
        self.assert_equals(len(desc), 3, "Should have 3 columns")

        # 列名非空
        self.assert_not_none(desc[0][0], "Column 1 name should not be None")
        self.assert_not_none(desc[1][0], "Column 2 name should not be None")
        self.assert_not_none(desc[2][0], "Column 3 name should not be None")

        # 关键: STRING 字面量列 'test' 上报为 SQL_WVARCHAR, 让 Power BI / Wide-API
        # 消费者通过 SQL_C_WCHAR 直接拿 UTF-16, 避免 zh-CN Windows 上的 GBK 误解码.
        self.assert_equals(desc[1][1], pyodbc.SQL_WVARCHAR,
                           "STRING literal column should report SQL_WVARCHAR")
        # 整数 / 浮点列保留旧映射
        self.assert_equals(desc[0][1], pyodbc.SQL_BIGINT,
                           "Integer literal column should report SQL_BIGINT")
        self.assert_equals(desc[2][1], pyodbc.SQL_DOUBLE,
                           "Float literal column should report SQL_DOUBLE")

        conn.close()
    
    def test_sqlcolattribute(self):
        """测试 SQLColAttribute - 获取列属性"""
        conn = self.connect()
        cursor = conn.cursor()

        # 同时验证 STRING / 整数两条映射, 因为 Power BI 走的是 SQLColAttribute 探测.
        cursor.execute("SELECT 1 as test_col, 'hello' as text_col")

        # pyodbc does not have getcolattr method
        # Use cursor.description to get column attributes
        desc = cursor.description
        self.assert_not_none(desc, "Cursor description should not be None")
        self.assert_equals(len(desc), 2, "Should have 2 columns")

        # Column name is at index 0
        self.assert_not_none(desc[0][0], "Column name should not be None")
        self.assert_not_none(desc[1][0], "Column name should not be None")

        # Column type code is at index 1
        self.assert_equals(desc[0][1], pyodbc.SQL_BIGINT,
                           "Integer literal should report SQL_BIGINT")
        self.assert_equals(desc[1][1], pyodbc.SQL_WVARCHAR,
                           "STRING literal should report SQL_WVARCHAR")

        conn.close()
    
    def test_sqlnumresultcols(self):
        """测试 SQLNumResultCols - 获取结果列数"""
        conn = self.connect()
        cursor = conn.cursor()
        
        cursor.execute("SELECT 1, 2, 3")
        
        # 获取列数
        num_cols = cursor.description
        self.assert_equals(len(num_cols), 3, "Should have 3 columns")
        
        conn.close()
    
    def test_cursor_description(self):
        """测试 cursor.description"""
        conn = self.connect()
        cursor = conn.cursor()
        
        cursor.execute("SELECT 1 as col1, 'test' as col2, 3.14 as col3")
        
        # 检查描述
        description = cursor.description
        self.assert_equals(len(description), 3, "Should have 3 columns")
        
        # 检查列名
        col_names = [col[0] for col in description]
        self.assert_in('col1', col_names, "col1 should be in column names")
        self.assert_in('col2', col_names, "col2 should be in column names")
        self.assert_in('col3', col_names, "col3 should be in column names")
        
        conn.close()
    
    def test_sqlrowcount(self):
        """测试 SQLRowCount - 获取受影响的行数"""
        conn = self.connect()
        cursor = conn.cursor()

        cursor.execute("SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3")
        rows = cursor.fetchall()

        # pyodbc's cursor.rowcount may return -1 or 0 for SELECT statements
        # We verify row count by checking the actual fetched rows
        row_count = len(rows)
        self.assert_greater(row_count, 0, "Row count should be greater than 0")

        conn.close()
    
    def test_table_types(self):
        """测试表类型"""
        conn = self.connect()
        cursor = conn.cursor()
        
        # 获取所有表类型
        cursor.tables()
        all_tables = cursor.fetchall()
        
        # 获取只有 TABLE 类型的表
        cursor.tables(tableType='TABLE')
        tables = cursor.fetchall()
        
        # 获取 VIEW 类型的表
        cursor.tables(tableType='VIEW')
        views = cursor.fetchall()
        
        self.assert_greater(len(all_tables), 0, "Should return some tables")
        
        conn.close()
    
    def test_column_info(self):
        """测试列详细信息"""
        conn = self.connect()
        cursor = conn.cursor()
        
        # 执行查询
        cursor.execute("SELECT CAST(1 AS BIGINT) as bigint_col, CAST('test' AS STRING) as str_col")
        
        # 获取列信息
        for i in range(1, 3):
            name, type_code, display_size, internal_size, precision, scale, nullable = cursor.description[i-1]
            self.assert_not_none(name, f"Column {i} name should not be None")
        
        conn.close()
    
    def test_schema_tables(self):
        """测试获取特定 schema 的表"""
        conn = self.connect()
        cursor = conn.cursor()
        
        # 获取 default 的表
        cursor.tables(catalog=None, schema='default', table=None, tableType='TABLE')
        tables = cursor.fetchall()
        
        self.assert_greater(len(tables), 0, "default schema should have tables")
        
        conn.close()

def run_all_tests():
    """运行所有元数据测试"""
    print_section("Metadata Tests")
    
    # 检查配置
    from config import config
    is_valid, message = config.validate()
    if not is_valid:
        print(f"✗ Configuration error: {message}")
        return False
    
    # 运行测试
    test = TestMetadata()
    
    tests = [
        ("SQLTables", test.test_sqltables),
        ("SQLColumns", test.test_sqlcolumns),
        ("SQLDescribeCol", test.test_sqldescribecol),
        ("SQLColAttribute", test.test_sqlcolattribute),
        ("SQLNumResultCols", test.test_sqlnumresultcols),
        ("Cursor Description", test.test_cursor_description),
        ("SQLRowCount", test.test_sqlrowcount),
        ("Table Types", test.test_table_types),
        ("Column Info", test.test_column_info),
        ("Schema Tables", test.test_schema_tables),
    ]
    
    for test_name, test_func in tests:
        test.run_test(test_func, test_name)
    
    return test.print_results()


if __name__ == "__main__":
    import pyodbc
    success = run_all_tests()
    sys.exit(0 if success else 1)