# gdbcmd.py
import gdb

# 定义要执行的命令
def setup_breakpoints():
    gdb.execute("break co_wait")
    #gdb.execute("b test_1")
    #gdb.execute("b test_2")
    gdb.execute("b test_3")
    #gdb.execute("b coroutine_wrapper")
    #gdb.execute("break work_loop")
    gdb.execute("run")
    gdb.execute("bt")
    gdb.execute("b co.c:68")

    #gdb.execute("b printf")

# 注册命令
setup_breakpoints()

#使用方法
#gdb -x gdbcmd.py ./libco-test-64