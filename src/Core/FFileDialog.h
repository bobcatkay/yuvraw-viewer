#pragma once

#include <string>
#include <vector>

/**
 * Win32 文件/目录选择对话框与系统 Shell 操作
 *
 * 用系统自带的 IFileOpenDialog（COM），不引入第三方库。
 * ImGui 没有原生文件对话框，而自绘一个既难看又缺少"最近位置/快捷方式/网络路径"等系统能力。
 */
namespace FFileDialog
{
    /**
     * 打开文件选择对话框
     * @param Title       标题
     * @param Extensions  允许的扩展名（含点号，全小写）。为空表示所有文件
     * @param OutPath     选中的路径
     * @return 用户取消或出错时返回 false
     */
    bool OpenFile(const std::string& Title, const std::vector<std::string>& Extensions, std::string& OutPath);

    /**
     * 打开目录选择对话框
     * @param Title    标题
     * @param OutPath  选中的目录
     * @return 用户取消或出错时返回 false
     */
    bool OpenDirectory(const std::string& Title, std::string& OutPath);

    /**
     * 打开资源管理器并选中一个文件
     *
     * 导出完成后点击结果里的文件名走这条路：告诉用户"存到哪儿了"，
     * 光贴一行路径还得他自己去翻。
     *
     * @param Path 文件的绝对路径（UTF-8）
     * @return 路径不存在或 Shell 调用失败时返回 false
     */
    bool RevealInExplorer(const std::string& Path);

    /** 用默认浏览器打开项目 Releases 页面；Shell 调用失败时返回 false。 */
    bool OpenProjectReleases();

    /** 用默认浏览器打开项目的新建 Issue 页面；Shell 调用失败时返回 false。 */
    bool OpenProjectIssue();

    /** 在资源管理器中打开当前日志目录；目录不可访问或 Shell 调用失败时返回 false。 */
    bool OpenLogDirectory();
}
