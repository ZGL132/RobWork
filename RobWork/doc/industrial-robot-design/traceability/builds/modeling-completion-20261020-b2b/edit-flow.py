from pathlib import Path
p=Path('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp')
s=p.read_text(encoding='utf-8-sig');a=s.index('        // 复位目标参考＝位姿集保留键',s.index('if (commandId == "modeling.reset-home-zero")'));b=s.index('\n    }',a)
s=s[:a]+'''        // MDL-17：只显示权威保留键；旧home/zero不是别名，不得静默选中。
        // 保存条目由模块装配出口恢复到真实宿主，取消/缺出口不写任何会话态。
        if(!deps.restoreNamedPose){summary="会话姿态恢复未装配——未写入";return false;}
        if(!session.draft.poseSetObject){summary="命名位姿集缺失——无可恢复的Home/Zero";return false;}
        QStringList choices;
        for(const auto key:{kHomeConfigurationPoseKey,kZeroConfigurationPoseKey}){
            if(std::any_of(session.draft.poseSetObject->entries.begin(),session.draft.poseSetObject->entries.end(),
                [&](const auto& e){return e.key==key;})){
                choices.push_back(QString::fromStdString(std::string(key)));
            }
        }
        if(choices.empty()){summary="未定义homeConfiguration/zeroConfiguration——未写入";return false;}
        const auto selected=host.chooseItem(QStringLiteral("恢复Home/Zero"),
            QStringLiteral("选择保存的姿态（仅改变已应用模型的会话姿态）"),choices);
        if(!selected){summary.clear();return false;}
        if(*selected<0||*selected>=choices.size()){summary="姿态选择下标无效——未写入";return false;}
        return deps.restoreNamedPose(choices[*selected].toStdString(),summary);'''+s[b:]
p.write_text(s,encoding='utf-8')
