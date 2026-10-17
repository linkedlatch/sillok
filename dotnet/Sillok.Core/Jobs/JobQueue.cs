namespace Sillok.Core;

public sealed class JobQueue
{
    private readonly object gate = new();
    private readonly SynchronizationContext? context = SynchronizationContext.Current;
    private readonly List<(Guid Id, Func<CancellationToken, Task> Work)> pending = [];
    private readonly HashSet<Guid> rerun = [];
    private (Guid Id, Func<CancellationToken, Task> Work, CancellationTokenSource Cancel)? running;

    public event Action? Idle;

    public IReadOnlyList<Guid> PendingIds
    {
        get
        {
            lock (gate) return pending.Select(job => job.Id).ToList();
        }
    }

    public Guid? RunningId
    {
        get
        {
            lock (gate) return running?.Id;
        }
    }

    public void Enqueue(Guid id, Func<CancellationToken, Task> work)
    {
        lock (gate)
        {
            if (running?.Id == id)
            {
                rerun.Add(id);
                return;
            }
            if (pending.Any(job => job.Id == id)) return;
            pending.Add((id, work));
        }
        StartNextIfIdle();
    }

    public void Cancel(Guid id)
    {
        lock (gate)
        {
            var index = pending.FindIndex(job => job.Id == id);
            if (index >= 0)
            {
                pending.RemoveAt(index);
            }
            else if (running?.Id == id)
            {
                rerun.Remove(id);
                running.Value.Cancel.Cancel();
            }
        }
    }

    private void StartNextIfIdle()
    {
        (Guid Id, Func<CancellationToken, Task> Work, CancellationTokenSource Cancel) job;
        lock (gate)
        {
            if (running is not null) return;
            if (pending.Count == 0)
            {
                Post(() => Idle?.Invoke());
                return;
            }
            var next = pending[0];
            pending.RemoveAt(0);
            job = (next.Id, next.Work, new CancellationTokenSource());
            running = job;
        }
        Post(() => _ = Execute(job));
    }

    private async Task Execute((Guid Id, Func<CancellationToken, Task> Work, CancellationTokenSource Cancel) job)
    {
        try
        {
            await job.Work(job.Cancel.Token);
        }
        catch (Exception)
        {
        }
        lock (gate)
        {
            if (rerun.Remove(job.Id)) pending.Insert(0, (job.Id, job.Work));
            running = null;
        }
        job.Cancel.Dispose();
        StartNextIfIdle();
    }

    private void Post(Action action)
    {
        if (context is null)
        {
            Task.Run(action);
        }
        else
        {
            context.Post(_ => action(), null);
        }
    }
}
