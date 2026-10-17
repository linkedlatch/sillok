namespace Sillok.Core;

public sealed class ProjectStore(string root)
{
    public string Directory(Guid id) => Path.Combine(root, id.ToString("D"));

    public string ProjectPath(Guid id) => Path.Combine(Directory(id), "project.json");

    public string CheckpointPath(Guid id) => Path.Combine(Directory(id), "checkpoint.json");

    public string AudioCachePath(Guid id) => Path.Combine(Directory(id), "audio.wav");

    public IReadOnlyList<TranscriptProject> LoadAll()
    {
        if (!System.IO.Directory.Exists(root)) return [];
        var projects = new List<TranscriptProject>();
        foreach (var folder in System.IO.Directory.EnumerateDirectories(root))
        {
            var path = Path.Combine(folder, "project.json");
            if (!File.Exists(path)) continue;
            try
            {
                projects.Add(VersionedStore.Load<TranscriptProject>(path));
            }
            catch (Exception)
            {
            }
        }
        return projects.OrderByDescending(project => project.CreatedAt).ToList();
    }

    public void Save(TranscriptProject project) => VersionedStore.Save(project, ProjectPath(project.Id));

    public void Delete(Guid id)
    {
        if (System.IO.Directory.Exists(Directory(id))) System.IO.Directory.Delete(Directory(id), recursive: true);
    }
}
