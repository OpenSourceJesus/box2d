using Godot;

// An Area2D wired by the scene's [connection]s.
public partial class Zone : Area2D
{
    private int _balls;
    private int _left;
    private int _ticks;

    public void OnEntered(Node2D body)
    {
        if (body.IsInGroup("balls"))
        {
            _balls = _balls + 1;
        }
        GD.Print("zone entered by ", body.Name);
    }

    public void OnExited(Node2D body)
    {
        if (body is Ball)
        {
            _left = _left + 1;
        }
    }

    public override void _PhysicsProcess(double delta)
    {
        _ticks = _ticks + 1;
        if (_ticks == 180)
        {
            GD.Print("zone balls=", _balls, " left=", _left);
        }
    }
}
