#include "pch.h"
#include "Model.h"

static void VerifyBindings(
    const std::string& dispatchableType,
    const std::unordered_map<std::string, std::vector<Model::BufferBindingSource>>& initBindings,
    std::unordered_map<std::string, Model::ResourceDesc*>& resourceDescsByName)
{
    for (const auto& [bindingName, sourceResources] : initBindings)
    {
        for (const auto& sourceResource : sourceResources)
        {
            if (resourceDescsByName.find(sourceResource.name) == resourceDescsByName.end())
            {
                throw std::invalid_argument(fmt::format(
                    "{} dispatchable attempts to bind resource '{}' for initialization, which does not exist in the model", 
                    dispatchableType, sourceResource.name));
            }
        }
    }
}

Model::Model(
    std::vector<ResourceDesc>&& resourceDescs,
    std::vector<DispatchableDesc>&& dispatchableDescs,
    std::vector<CommandDesc>&& commands,
    BucketAllocator&& allocator) : 
        m_resourceDescs(std::move(resourceDescs)),
        m_dispatchableDescs(std::move(dispatchableDescs)),
        m_commands(std::move(commands)),
        m_allocator(std::move(allocator))
{
    for (auto& resourceDesc : m_resourceDescs) 
    {
        m_resourceDescsByName[resourceDesc.name] = &resourceDesc;
    }

    for (auto& dispatchableDesc : m_dispatchableDescs) 
    { 
        m_dispatchableDescsByName[dispatchableDesc.name] = &dispatchableDesc;
        
        if (std::holds_alternative<DmlDispatchableDesc>(dispatchableDesc.value))
        {
            VerifyBindings("DML", std::get<DmlDispatchableDesc>(dispatchableDesc.value).initBindings, m_resourceDescsByName);
        }
        else if (std::holds_alternative<DmlSerializedGraphDispatchableDesc>(dispatchableDesc.value))
        {
            VerifyBindings("DmlSerializedGraph", std::get<DmlSerializedGraphDispatchableDesc>(dispatchableDesc.value).initBindings, m_resourceDescsByName);
        }

    }

    // Validate references to ops/resources in the model.
    for (auto& commandDesc : m_commands)
    {
        auto& command = commandDesc.command;
        std::visit(
            overload{
                [&](DispatchCommand& command)
                {
                    auto dispatchable = m_dispatchableDescsByName.find(command.dispatchableName);
                    if (dispatchable == m_dispatchableDescsByName.end())
                    {
                        throw std::invalid_argument(fmt::format(
                            "Command attempts to dispatch '{}', which does not exist in the model", 
                            command.dispatchableName));
                    }
                
                    for (auto& binding : command.bindings)
                    {
                        for (auto& sourceResource : binding.second)
                        {
                            if (m_resourceDescsByName.find(sourceResource.name) == m_resourceDescsByName.end())
                            {
                                throw std::invalid_argument(fmt::format(
                                    "Command attempts to bind resource '{}', which does not exist in the model", 
                                    sourceResource.name));
                            }

                            if (sourceResource.counterName && m_resourceDescsByName.find(*sourceResource.counterName) == m_resourceDescsByName.end())
                            {
                                throw std::invalid_argument(fmt::format(
                                    "Command attempts to bind resource '{}' as a counter, which does not exist in the model", 
                                    *sourceResource.counterName));
                            }
                        }
                    }
                },
                [&](PrintCommand& printCommand)
                {
                    if (m_resourceDescsByName.find(printCommand.resourceName) == m_resourceDescsByName.end())
                    {
                        throw std::invalid_argument(fmt::format(
                            "Command attempts to print resource '{}', which does not exist in the model", 
                            printCommand.resourceName));
                    }
                },
                [&](WriteFileCommand& writeFileCommand)
                {
                    if (m_resourceDescsByName.find(writeFileCommand.resourceName) == m_resourceDescsByName.end())
                    {
                        throw std::invalid_argument(fmt::format(
                            "Command attempts to write to a file the resource '{}', which does not exist in the model", 
                            writeFileCommand.resourceName));
                    }
                }
            },
            command);
    }
}