# 操作系统真象还原
操作系统真象还原是一本自制操作系统的书. 这里是读这本书的过程. 

## 施工记录
* 2024/9/20, 读完第三章, 写完MBR和loader, 目前没觉得这本书很好, 作者想讲的详细, 导致内容庞大抓不住重点.
* 2024/9/21, 读完4.3, 成功进入保护模式. 需要了解的概念有: GDT, 选择子
* 2024/9/24, 读完5, 启动分页机制, 加载内核
* 2024/9/24, 第六章, put char
* 2024/9/25, 过完第六章, put str, put int, 内联汇编. 文件逐渐多起来了, 应该用sh或者makefile来构建了 (第八章介绍makefile)
* 2024/9/27, 第七章第一部分, 用汇编完成中断功能
* 2024/9/28, 完成第七章, 时钟中断
* 2024/10/2, 第八章到ASSERT, 以及用makefile编译
* 2024/10/3, 第八章string系列函数, bitmap
* 2024/10/8, 第八章内存管理完成, 开始第九章线程
* 2024/10/8, 第九章, 线程, 侵入式链表, 接下来是多线程调度